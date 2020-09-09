/*
 * ------------------------------------------------------------------------------------------------------------
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * Copyright (c) 2018-2020 Lawrence Livermore National Security LLC
 * Copyright (c) 2018-2020 The Board of Trustees of the Leland Stanford Junior University
 * Copyright (c) 2018-2020 Total, S.A
 * Copyright (c) 2019-     GEOSX Contributors
 * All rights reserved
 *
 * See top level LICENSE, COPYRIGHT, CONTRIBUTORS, NOTICE, and ACKNOWLEDGEMENTS files for details.
 * ------------------------------------------------------------------------------------------------------------
 */

/**
 * @file CompositionalMultiphaseFVM.cpp
 */

#include "CompositionalMultiphaseFVM.hpp"

#include "common/DataTypes.hpp"
#include "common/TimingMacros.hpp"
#include "constitutive/ConstitutiveManager.hpp"
#include "constitutive/fluid/MultiFluidBase.hpp"
#include "constitutive/relativePermeability/RelativePermeabilityBase.hpp"
#include "constitutive/capillaryPressure/CapillaryPressureBase.hpp"
#include "dataRepository/Group.hpp"
#include "finiteVolume/FiniteVolumeManager.hpp"
#include "finiteVolume/FluxApproximationBase.hpp"
#include "managers/DomainPartition.hpp"
#include "managers/NumericalMethodsManager.hpp"
#include "mpiCommunications/CommunicationTools.hpp"
#include "mpiCommunications/NeighborCommunicator.hpp"
#include "mpiCommunications/MpiWrapper.hpp"
#include "physicsSolvers/fluidFlow/CompositionalMultiphaseBaseKernels.hpp"
#include "physicsSolvers/fluidFlow/CompositionalMultiphaseFVMKernels.hpp"

namespace geosx
{

using namespace dataRepository;
using namespace constitutive;
using namespace CompositionalMultiphaseFVMKernels;
using namespace CompositionalMultiphaseBaseKernels;

CompositionalMultiphaseFVM::CompositionalMultiphaseFVM( const string & name,
                                                        Group * const parent )
  :
  CompositionalMultiphaseBase( name, parent )
{

  m_linearSolverParameters.get().mgr.strategy = "CompositionalMultiphaseFVM";

}


void CompositionalMultiphaseFVM::SetupDofs( DomainPartition const & domain,
                                            DofManager & dofManager ) const
{
  dofManager.addField( viewKeyStruct::elemDofFieldString,
                       DofManager::Location::Elem,
                       m_numDofPerCell,
                       targetRegionNames() );

  NumericalMethodsManager const & numericalMethodManager = domain.getNumericalMethodManager();
  FiniteVolumeManager const & fvManager = numericalMethodManager.getFiniteVolumeManager();
  FluxApproximationBase const & fluxApprox = fvManager.getFluxApproximation( m_discretizationName );

  dofManager.addCoupling( viewKeyStruct::elemDofFieldString, fluxApprox );
}


void CompositionalMultiphaseFVM::AssembleFluxTerms( real64 const dt,
                                                    DomainPartition const & domain,
                                                    DofManager const & dofManager,
                                                    CRSMatrixView< real64, globalIndex const > const & localMatrix,
                                                    arrayView1d< real64 > const & localRhs ) const
{
  GEOSX_MARK_FUNCTION;

  MeshLevel const & mesh = *domain.getMeshBody( 0 )->getMeshLevel( 0 );

  /*
   * Force phase compositions to be moved to device.
   *
   * An issue with ElementViewAccessors is that if the outer arrays are already on device,
   * but an inner array gets touched and updated on host, capturing outer arrays in a device kernel
   * DOES NOT call move() on the inner array (see implementation of NewChaiBuffer::moveNested()).
   * Here we force the move by launching a dummy kernel.
   *
   * This is not a problem in normal solver execution, as these arrays get moved by AccumulationKernel.
   * But it fails unit tests, which test flux assembly separately.
   *
   * TODO: See if this can be fixed in NewChaiBuffer (I have not found a way - Sergey).
   *       Alternatively, stop using ElementViewAccessors altogether and just roll with
   *       accessors' outer arrays being moved on every jacobian assembly (maybe disable output).
   *       Or stop testing through the solver interface and test separate kernels instead.
   *       Finally, the problem should go away when fluid updates are executed on device.
   */
  forTargetSubRegions( mesh, [&]( localIndex const targetIndex, ElementSubRegionBase const & subRegion )
  {
    MultiFluidBase const & fluid = GetConstitutiveModel< MultiFluidBase >( subRegion, fluidModelNames()[targetIndex] );
    arrayView4d< real64 const > const & phaseCompFrac = fluid.phaseCompFraction();
    arrayView4d< real64 const > const & dPhaseCompFrac_dPres = fluid.dPhaseCompFraction_dPressure();
    arrayView5d< real64 const > const & dPhaseCompFrac_dComp = fluid.dPhaseCompFraction_dGlobalCompFraction();

    forAll< parallelDevicePolicy<> >( subRegion.size(),
                                      [phaseCompFrac, dPhaseCompFrac_dPres, dPhaseCompFrac_dComp]
                                      GEOSX_HOST_DEVICE ( localIndex const )
    {
      GEOSX_UNUSED_VAR( phaseCompFrac )
      GEOSX_UNUSED_VAR( dPhaseCompFrac_dPres )
      GEOSX_UNUSED_VAR( dPhaseCompFrac_dComp )
    } );
  } );

  NumericalMethodsManager const & numericalMethodManager = domain.getNumericalMethodManager();
  FiniteVolumeManager const & fvManager = numericalMethodManager.getFiniteVolumeManager();
  FluxApproximationBase const & fluxApprox = fvManager.getFluxApproximation( m_discretizationName );

  string const & dofKey = dofManager.getKey( viewKeyStruct::elemDofFieldString );
  ElementRegionManager::ElementViewAccessor< arrayView1d< globalIndex const > >
  elemDofNumber = mesh.getElemManager()->ConstructArrayViewAccessor< globalIndex, 1 >( dofKey );
  elemDofNumber.setName( getName() + "/accessors/" + dofKey );

  fluxApprox.forAllStencils( mesh, [&] ( auto const & stencil )
  {
    KernelLaunchSelector1< FluxKernel >( m_numComponents,
                                         m_numPhases,
                                         stencil,
                                         dofManager.rankOffset(),
                                         elemDofNumber.toViewConst(),
                                         m_elemGhostRank.toViewConst(),
                                         m_pressure.toViewConst(),
                                         m_deltaPressure.toViewConst(),
                                         m_gravCoef.toViewConst(),
                                         m_phaseMob.toViewConst(),
                                         m_dPhaseMob_dPres.toViewConst(),
                                         m_dPhaseMob_dCompDens.toViewConst(),
                                         m_dPhaseVolFrac_dPres.toViewConst(),
                                         m_dPhaseVolFrac_dCompDens.toViewConst(),
                                         m_dCompFrac_dCompDens.toViewConst(),
                                         m_phaseDens.toViewConst(),
                                         m_dPhaseDens_dPres.toViewConst(),
                                         m_dPhaseDens_dComp.toViewConst(),
                                         m_phaseCompFrac.toViewConst(),
                                         m_dPhaseCompFrac_dPres.toViewConst(),
                                         m_dPhaseCompFrac_dComp.toViewConst(),
                                         m_phaseCapPressure.toViewConst(),
                                         m_dPhaseCapPressure_dPhaseVolFrac.toViewConst(),
                                         m_capPressureFlag,
                                         dt,
                                         localMatrix.toViewConstSizes(),
                                         localRhs.toView() );
  } );
}

real64 CompositionalMultiphaseFVM::CalculateResidualNorm( DomainPartition const & domain,
                                                          DofManager const & dofManager,
                                                          arrayView1d< real64 const > const & localRhs )
{
  localIndex const NDOF = m_numComponents + 1;

  MeshLevel const & mesh = *domain.getMeshBody( 0 )->getMeshLevel( 0 );
  real64 localResidualNorm = 0.0;

  globalIndex const rankOffset = dofManager.rankOffset();
  string const dofKey = dofManager.getKey( viewKeyStruct::elemDofFieldString );

  forTargetSubRegions( mesh, [&]( localIndex const targetIndex, ElementSubRegionBase const & subRegion )
  {
    MultiFluidBase const & fluid = GetConstitutiveModel< MultiFluidBase >( subRegion, m_fluidModelNames[targetIndex] );

    arrayView1d< globalIndex const > dofNumber = subRegion.getReference< array1d< globalIndex > >( dofKey );
    arrayView1d< integer const > const & elemGhostRank = subRegion.ghostRank();
    arrayView1d< real64 const > const & volume = subRegion.getElementVolume();
    arrayView1d< real64 const > const & refPoro = subRegion.getReference< array1d< real64 > >( viewKeyStruct::referencePorosityString );
    arrayView2d< real64 const > const & totalDens = fluid.totalDensity();

    RAJA::ReduceSum< parallelDeviceReduce, real64 > localSum( 0.0 );

    forAll< parallelDevicePolicy<> >( subRegion.size(), [=] GEOSX_HOST_DEVICE ( localIndex const ei )
    {
      if( elemGhostRank[ei] < 0 )
      {
        localIndex const localRow = dofNumber[ei] - rankOffset;
        real64 const normalizer = totalDens[ei][0] * refPoro[ei] * volume[ei];

        for( localIndex idof = 0; idof < NDOF; ++idof )
        {
          real64 const val = localRhs[localRow + idof] / normalizer;
          localSum += val * val;
        }
      }
    } );

    localResidualNorm += localSum.get();
  } );

  // compute global residual norm
  real64 const residual = std::sqrt( MpiWrapper::Sum( localResidualNorm ) );

  if( getLogLevel() >= 1 && logger::internal::rank==0 )
  {
    char output[200] = {0};
    sprintf( output, "    ( Rfluid ) = (%4.2e) ; ", residual );
    std::cout<<output;
  }

  return residual;
}

bool CompositionalMultiphaseFVM::CheckSystemSolution( DomainPartition const & domain,
                                                      DofManager const & dofManager,
                                                      arrayView1d< real64 const > const & localSolution,
                                                      real64 const scalingFactor )
{
  real64 constexpr eps = CompositionalMultiphaseBaseKernels::minDensForDivision;

  localIndex const NC = m_numComponents;
  integer const allowCompDensChopping = m_allowCompDensChopping;

  MeshLevel const & mesh = *domain.getMeshBody( 0 )->getMeshLevel( 0 );

  globalIndex const rankOffset = dofManager.rankOffset();
  string const dofKey = dofManager.getKey( viewKeyStruct::elemDofFieldString );
  int localCheck = 1;

  forTargetSubRegions( mesh, [&]( localIndex const, ElementSubRegionBase const & subRegion )
  {
    arrayView1d< globalIndex const > const & dofNumber = subRegion.getReference< array1d< globalIndex > >( dofKey );
    arrayView1d< integer const > const & elemGhostRank = subRegion.ghostRank();

    arrayView1d< real64 const > const & pres = subRegion.getReference< array1d< real64 > >( viewKeyStruct::pressureString );
    arrayView1d< real64 const > const & dPres = subRegion.getReference< array1d< real64 > >( viewKeyStruct::deltaPressureString );
    arrayView2d< real64 const > const & compDens = subRegion.getReference< array2d< real64 > >( viewKeyStruct::globalCompDensityString );
    arrayView2d< real64 const > const & dCompDens = subRegion.getReference< array2d< real64 > >( viewKeyStruct::deltaGlobalCompDensityString );

    RAJA::ReduceMin< parallelDeviceReduce, integer > check( 1 );

    forAll< parallelDevicePolicy<> >( subRegion.size(), [=] GEOSX_HOST_DEVICE ( localIndex const ei )
    {
      if( elemGhostRank[ei] < 0 )
      {
        localIndex const localRow = dofNumber[ei] - rankOffset;
        {
          real64 const newPres = pres[ei] + dPres[ei] + scalingFactor * localSolution[localRow];
          check.min( newPres >= 0.0 );
        }

        // if component density chopping is not allowed, the time step fails if a component density is negative
        // otherwise, we just check that the total density is positive, and negative component densities
        // will be chopped (i.e., set to zero) in ApplySystemSolution)
        if( !allowCompDensChopping )
        {
          for( localIndex ic = 0; ic < NC; ++ic )
          {
            real64 const newDens = compDens[ei][ic] + dCompDens[ei][ic] + scalingFactor * localSolution[localRow + ic + 1];
            check.min( newDens >= 0.0 );
          }
        }
        else
        {
          real64 totalDens = 0.0;
          for( localIndex ic = 0; ic < NC; ++ic )
          {
            real64 const newDens = compDens[ei][ic] + dCompDens[ei][ic] + scalingFactor * localSolution[localRow + ic + 1];
            totalDens += (newDens > 0.0) ? newDens : 0.0;
          }
          check.min( totalDens >= eps );
        }
      }
    } );

    localCheck = std::min( localCheck, check.get() );
  } );

  return MpiWrapper::Min( localCheck, MPI_COMM_GEOSX );
}

void CompositionalMultiphaseFVM::ApplySystemSolution( DofManager const & dofManager,
                                                      arrayView1d< real64 const > const & localSolution,
                                                      real64 const scalingFactor,
                                                      DomainPartition & domain )
{
  MeshLevel & mesh = *domain.getMeshBody( 0 )->getMeshLevel( 0 );
  dofManager.addVectorToField( localSolution,
                               viewKeyStruct::elemDofFieldString,
                               viewKeyStruct::deltaPressureString,
                               scalingFactor,
                               0, 1 );

  dofManager.addVectorToField( localSolution,
                               viewKeyStruct::elemDofFieldString,
                               viewKeyStruct::deltaGlobalCompDensityString,
                               scalingFactor,
                               1, m_numDofPerCell );

  // if component density chopping is allowed, some component densities may be negative after the update
  // these negative component densities are set to zero in this function
  if( m_allowCompDensChopping )
  {
    ChopNegativeDensities( domain );
  }

  std::map< string, string_array > fieldNames;
  fieldNames["elems"].emplace_back( string( viewKeyStruct::deltaPressureString ) );
  fieldNames["elems"].emplace_back( string( viewKeyStruct::deltaGlobalCompDensityString ) );
  CommunicationTools::SynchronizeFields( fieldNames, &mesh, domain.getNeighbors(), true );

  forTargetSubRegions( mesh, [&]( localIndex const targetIndex, ElementSubRegionBase & subRegion )
  {
    UpdateState( subRegion, targetIndex );
  } );
}

void CompositionalMultiphaseFVM::UpdatePhaseMobility( Group & dataGroup, localIndex const targetIndex ) const
{
  GEOSX_MARK_FUNCTION;

  // note that for convenience, the phase mobility computed here also includes phase density

  // outputs

  arrayView2d< real64 > const & phaseMob =
    dataGroup.getReference< array2d< real64 > >( viewKeyStruct::phaseMobilityString );

  arrayView2d< real64 > const & dPhaseMob_dPres =
    dataGroup.getReference< array2d< real64 > >( viewKeyStruct::dPhaseMobility_dPressureString );

  arrayView3d< real64 > const & dPhaseMob_dComp =
    dataGroup.getReference< array3d< real64 > >( viewKeyStruct::dPhaseMobility_dGlobalCompDensityString );

  // inputs

  arrayView2d< real64 const > const & dPhaseVolFrac_dPres =
    dataGroup.getReference< array2d< real64 > >( viewKeyStruct::dPhaseVolumeFraction_dPressureString );

  arrayView3d< real64 const > const & dPhaseVolFrac_dComp =
    dataGroup.getReference< array3d< real64 > >( viewKeyStruct::dPhaseVolumeFraction_dGlobalCompDensityString );

  arrayView3d< real64 const > const & dCompFrac_dCompDens =
    dataGroup.getReference< array3d< real64 > >( viewKeyStruct::dGlobalCompFraction_dGlobalCompDensityString );

  MultiFluidBase const & fluid = GetConstitutiveModel< MultiFluidBase >( dataGroup, m_fluidModelNames[targetIndex] );

  arrayView3d< real64 const > const & phaseDens = fluid.phaseDensity();
  arrayView3d< real64 const > const & dPhaseDens_dPres = fluid.dPhaseDensity_dPressure();
  arrayView4d< real64 const > const & dPhaseDens_dComp = fluid.dPhaseDensity_dGlobalCompFraction();

  arrayView3d< real64 const > const & phaseVisc = fluid.phaseViscosity();
  arrayView3d< real64 const > const & dPhaseVisc_dPres = fluid.dPhaseViscosity_dPressure();
  arrayView4d< real64 const > const & dPhaseVisc_dComp = fluid.dPhaseViscosity_dGlobalCompFraction();

  RelativePermeabilityBase const & relperm = GetConstitutiveModel< RelativePermeabilityBase >( dataGroup, m_relPermModelNames[targetIndex] );

  arrayView3d< real64 const > const & phaseRelPerm = relperm.phaseRelPerm();
  arrayView4d< real64 const > const & dPhaseRelPerm_dPhaseVolFrac = relperm.dPhaseRelPerm_dPhaseVolFraction();

  KernelLaunchSelector2< PhaseMobilityKernel >( m_numComponents, m_numPhases,
                                                dataGroup.size(),
                                                dCompFrac_dCompDens,
                                                phaseDens,
                                                dPhaseDens_dPres,
                                                dPhaseDens_dComp,
                                                phaseVisc,
                                                dPhaseVisc_dPres,
                                                dPhaseVisc_dComp,
                                                phaseRelPerm,
                                                dPhaseRelPerm_dPhaseVolFrac,
                                                dPhaseVolFrac_dPres,
                                                dPhaseVolFrac_dComp,
                                                phaseMob,
                                                dPhaseMob_dPres,
                                                dPhaseMob_dComp );
}


REGISTER_CATALOG_ENTRY( SolverBase, CompositionalMultiphaseFVM, string const &, Group * const )
}// namespace geosx