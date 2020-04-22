/*
 * ------------------------------------------------------------------------------------------------------------
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 * Copyright (c) 2018-2019 Lawrence Livermore National Security LLC
 * Copyright (c) 2018-2019 The Board of Trustees of the Leland Stanford Junior University
 * Copyright (c) 2018-2019 Total, S.A
 * Copyright (c) 2019-     GEOSX Contributors
 * All right reserved
 *
 * See top level LICENSE, COPYRIGHT, CONTRIBUTORS, NOTICE, and ACKNOWLEDGEMENTS files for details.
 * ------------------------------------------------------------------------------------------------------------
 */

/**
 * @file CompositionalMultiphaseFVMKernels.hpp
 */

#ifndef GEOSX_PHYSICSSOLVERS_FLUIDFLOW_COMPOSITIONALMULTIPHASEFVMKERNELS_HPP
#define GEOSX_PHYSICSSOLVERS_FLUIDFLOW_COMPOSITIONALMULTIPHASEFVMKERNELS_HPP

#include "common/DataTypes.hpp"
#include "constitutive/fluid/MultiFluidBase.hpp"
#include "finiteVolume/FluxApproximationBase.hpp"
#include "rajaInterface/GEOS_RAJA_Interface.hpp"

namespace geosx
{

namespace CompositionalMultiphaseFVMKernels
{


/******************************** FluxKernel ********************************/

/**
 * @brief Functions to assemble flux term contributions to residual and Jacobian
 */
struct FluxKernel
{

  /**
   * @brief The type for element-based non-constitutive data parameters.
   * Consists entirely of ArrayView's.
   *
   * Can be converted from ElementRegionManager::ElementViewAccessor
   * by calling .toView() or .toViewConst() on an accessor instance
   */
  template< typename VIEWTYPE >
  using ElementView = typename ElementRegionManager::ElementViewAccessor< VIEWTYPE >::ViewTypeConst;

  /**
   * @brief The type for element-based constitutive data parameters.
   * Consists entirely of ArrayView's.
   *
   * Can be converted from ElementRegionManager::MaterialViewAccessor
   * by calling .toView() or .toViewConst() on an accessor instance
   */
  template< typename VIEWTYPE >
  using MaterialView = typename ElementRegionManager::MaterialViewAccessor< VIEWTYPE >::ViewTypeConst;

  static inline void
  Compute( localIndex const NC, localIndex const NP,
           localIndex const stencilSize,
           arraySlice1d< localIndex const > const & seri,
           arraySlice1d< localIndex const > const & sesri,
           arraySlice1d< localIndex const > const & sei,
           arraySlice1d< real64 const > const & stencilWeights,
           ElementView< arrayView1d< real64 const > > const & pres,
           ElementView< arrayView1d< real64 const > > const & dPres,
           ElementView< arrayView1d< real64 const > > const & gravCoef,
           ElementView< arrayView2d< real64 const > > const & phaseMob,
           ElementView< arrayView2d< real64 const > > const & dPhaseMob_dPres,
           ElementView< arrayView3d< real64 const > > const & dPhaseMob_dComp,
           ElementView< arrayView2d< real64 const > > const & dPhaseVolFrac_dPres,
           ElementView< arrayView3d< real64 const > > const & dPhaseVolFrac_dComp,
           ElementView< arrayView3d< real64 const > > const & dCompFrac_dCompDens,
           MaterialView< arrayView3d< real64 const > > const & phaseDens,
           MaterialView< arrayView3d< real64 const > > const & dPhaseDens_dPres,
           MaterialView< arrayView4d< real64 const > > const & dPhaseDens_dComp,
           MaterialView< arrayView4d< real64 const > > const & phaseCompFrac,
           MaterialView< arrayView4d< real64 const > > const & dPhaseCompFrac_dPres,
           MaterialView< arrayView5d< real64 const > > const & dPhaseCompFrac_dComp,
           MaterialView< arrayView3d< real64 const > > const & phaseCapPressure,
           MaterialView< arrayView4d< real64 const > > const & dPhaseCapPressure_dPhaseVolFrac,
           localIndex const fluidIndex,
           localIndex const capPressureIndex,
           integer const capPressureFlag,
           real64 const dt,
           arraySlice1d< real64 > const & localFlux,
           arraySlice2d< real64 > const & localFluxJacobian )
  {
    localIndex constexpr numElems   = CellElementStencilTPFA::NUM_POINT_IN_FLUX;
    localIndex constexpr maxStencil = CellElementStencilTPFA::MAX_STENCIL_SIZE;
    localIndex constexpr maxNumComp = constitutive::MultiFluidBase::MAX_NUM_COMPONENTS;

    localIndex const NDOF = NC + 1;

    // create local work arrays

    stackArray1d< real64, maxNumComp > dPhaseCompFrac_dCompDens( NC );

    stackArray1d< real64, maxStencil >              dPhaseFlux_dP( stencilSize );
    stackArray2d< real64, maxStencil * maxNumComp > dPhaseFlux_dC( stencilSize, NC );

    stackArray1d< real64, maxNumComp >                           compFlux( NC );
    stackArray2d< real64, maxStencil * maxNumComp >              dCompFlux_dP( stencilSize, NC );
    stackArray3d< real64, maxStencil * maxNumComp * maxNumComp > dCompFlux_dC( stencilSize, NC, NC );

    stackArray1d< real64, maxNumComp > dCapPressure_dC( NC );
    stackArray1d< real64, maxNumComp > dDens_dC( NC );

    stackArray1d< real64, numElems >              dDensMean_dP( numElems );
    stackArray2d< real64, numElems * maxNumComp > dDensMean_dC( numElems, NC );

    stackArray1d< real64, maxStencil >              dPresGrad_dP( stencilSize );
    stackArray2d< real64, maxStencil * maxNumComp > dPresGrad_dC( stencilSize, NC );

    stackArray1d< real64, numElems >                dGravHead_dP( numElems );
    stackArray2d< real64, numElems * maxNumComp >   dGravHead_dC( numElems, NC );

    // reset the local values to zero
    compFlux = 0.0;
    dCompFlux_dP = 0.0;
    dCompFlux_dC = 0.0;

    for( localIndex i = 0; i < numElems * NC; ++i )
    {
      localFlux[i] = 0.0;
      for( localIndex j = 0; j < stencilSize * NDOF; ++j )
      {
        localFluxJacobian[i][j] = 0.0;
      }
    }

    // loop over phases, compute and upwind phase flux and sum contributions to each component's flux
    for( localIndex ip = 0; ip < NP; ++ip )
    {
      // clear working arrays
      real64 densMean = 0.0;
      dDensMean_dP = 0.0;
      dDensMean_dC = 0.0;

      real64 presGrad = 0.0;
      dPresGrad_dP = 0.0;
      dPresGrad_dC = 0.0;

      real64 gravHead = 0.0;
      dGravHead_dP = 0.0;
      dGravHead_dC = 0.0;

      real64 phaseFlux;
      dPhaseFlux_dP = 0.0;
      dPhaseFlux_dC = 0.0;

      // calculate quantities on primary connected cells
      for( localIndex i = 0; i < numElems; ++i )
      {
        localIndex const er  = seri[i];
        localIndex const esr = sesri[i];
        localIndex const ei  = sei[i];

        // density
        real64 const density  = phaseDens[er][esr][fluidIndex][ei][0][ip];
        real64 const dDens_dP = dPhaseDens_dPres[er][esr][fluidIndex][ei][0][ip];

        applyChainRule( NC,
                        dCompFrac_dCompDens[er][esr][ei],
                        dPhaseDens_dComp[er][esr][fluidIndex][ei][0][ip],
                        dDens_dC );

        // average density and pressure derivative
        densMean += 0.5 * density;
        dDensMean_dP[i] = 0.5 * dDens_dP;

        // compositional derivatives
        for( localIndex jc = 0; jc < NC; ++jc )
        {
          dDensMean_dC[i][jc] = 0.5 * dDens_dC[jc];
        }
      }

      //***** calculation of flux *****

      // compute potential difference MPFA-style
      for( localIndex i = 0; i < stencilSize; ++i )
      {
        localIndex const er  = seri[i];
        localIndex const esr = sesri[i];
        localIndex const ei  = sei[i];
        real64 weight = stencilWeights[i];

        //capillary pressure
        real64 capPressure     = 0.0;
        real64 dCapPressure_dP = 0.0;
        dCapPressure_dC = 0.0;

        if( capPressureFlag )
        {
          capPressure = phaseCapPressure[er][esr][capPressureIndex][ei][0][ip];

          for( localIndex jp = 0; jp < NP; ++jp )
          {
            real64 const dCapPressure_dS = dPhaseCapPressure_dPhaseVolFrac[er][esr][capPressureIndex][ei][0][ip][jp];
            dCapPressure_dP += dCapPressure_dS * dPhaseVolFrac_dPres[er][esr][ei][jp];

            for( localIndex jc = 0; jc < NC; ++jc )
            {
              dCapPressure_dC[jc] += dCapPressure_dS * dPhaseVolFrac_dComp[er][esr][ei][jp][jc];
            }
          }
        }

        presGrad += weight * (pres[er][esr][ei] + dPres[er][esr][ei] - capPressure);
        dPresGrad_dP[i] += weight * (1 - dCapPressure_dP);
        for( localIndex jc = 0; jc < NC; ++jc )
        {
          dPresGrad_dC[i][jc] += -weight * dCapPressure_dC[jc];
        }

        real64 const gravD = weight * gravCoef[er][esr][ei];
        gravHead += densMean * gravD;

        // need to add contributions from both cells the mean density depends on
        for( localIndex j = 0; j < numElems; ++j )
        {
          dGravHead_dP[j] += dDensMean_dP[j] * gravD;
          for( localIndex jc = 0; jc < NC; ++jc )
          {
            dGravHead_dC[j][jc] += dDensMean_dC[j][jc] * gravD;
          }
        }
      }

      // *** upwinding ***

      // use PPU currently; advanced stuff like IHU would go here
      // TODO isolate into a kernel?

      // compute phase potential gradient
      real64 potGrad = presGrad - gravHead;

      // choose upstream cell
      localIndex const k_up = (potGrad >= 0) ? 0 : 1;

      localIndex er_up  = seri[k_up];
      localIndex esr_up = sesri[k_up];
      localIndex ei_up  = sei[k_up];

      real64 const mobility = phaseMob[er_up][esr_up][ei_up][ip];

      // skip the phase flux if phase not present or immobile upstream
      if( std::fabs( mobility ) < 1e-20 ) // TODO better constant
      {
        continue;
      }

      // pressure gradient depends on all points in the stencil
      for( localIndex ke = 0; ke < stencilSize; ++ke )
      {
        dPhaseFlux_dP[ke] += dPresGrad_dP[ke];
        for( localIndex jc = 0; jc < NC; ++jc )
        {
          dPhaseFlux_dC[ke][jc] += dPresGrad_dC[ke][jc];
        }

      }

      // gravitational head depends only on the two cells connected (same as mean density)
      for( localIndex ke = 0; ke < numElems; ++ke )
      {
        dPhaseFlux_dP[ke] -= dGravHead_dP[ke];
        for( localIndex jc = 0; jc < NC; ++jc )
        {
          dPhaseFlux_dC[ke][jc] -= dGravHead_dC[ke][jc];
        }
      }

      // compute the phase flux and derivatives using upstream cell mobility
      phaseFlux = mobility * potGrad;
      for( localIndex ke = 0; ke < stencilSize; ++ke )
      {
        dPhaseFlux_dP[ke] *= mobility;
        for( localIndex jc = 0; jc < NC; ++jc )
        {
          dPhaseFlux_dC[ke][jc] *= mobility;
        }
      }

      real64 const dMob_dP  = dPhaseMob_dPres[er_up][esr_up][ei_up][ip];
      arraySlice1d< real64 const > dPhaseMob_dCompSub = dPhaseMob_dComp[er_up][esr_up][ei_up][ip];

      // add contribution from upstream cell mobility derivatives
      dPhaseFlux_dP[k_up] += dMob_dP * potGrad;
      for( localIndex jc = 0; jc < NC; ++jc )
      {
        dPhaseFlux_dC[k_up][jc] += dPhaseMob_dCompSub[jc] * potGrad;
      }

      // slice some constitutive arrays to avoid too much indexing in component loop
      arraySlice1d< real64 const > phaseCompFracSub = phaseCompFrac[er_up][esr_up][fluidIndex][ei_up][0][ip];
      arraySlice1d< real64 const > dPhaseCompFrac_dPresSub = dPhaseCompFrac_dPres[er_up][esr_up][fluidIndex][ei_up][0][ip];
      arraySlice2d< real64 const > dPhaseCompFrac_dCompSub = dPhaseCompFrac_dComp[er_up][esr_up][fluidIndex][ei_up][0][ip];

      // compute component fluxes and derivatives using upstream cell composition
      for( localIndex ic = 0; ic < NC; ++ic )
      {
        real64 const ycp = phaseCompFracSub[ic];
        compFlux[ic] += phaseFlux * ycp;

        // derivatives stemming from phase flux
        for( localIndex ke = 0; ke < stencilSize; ++ke )
        {
          dCompFlux_dP[ke][ic] += dPhaseFlux_dP[ke] * ycp;
          for( localIndex jc = 0; jc < NC; ++jc )
          {
            dCompFlux_dC[ke][ic][jc] += dPhaseFlux_dC[ke][jc] * ycp;
          }
        }

        // additional derivatives stemming from upstream cell phase composition
        dCompFlux_dP[k_up][ic] += phaseFlux * dPhaseCompFrac_dPresSub[ic];

        // convert derivatives of component fraction w.r.t. component fractions to derivatives w.r.t. component
        // densities
        applyChainRule( NC, dCompFrac_dCompDens[er_up][esr_up][ei_up], dPhaseCompFrac_dCompSub[ic], dPhaseCompFrac_dCompDens );
        for( localIndex jc = 0; jc < NC; ++jc )
        {
          dCompFlux_dC[k_up][ic][jc] += phaseFlux * dPhaseCompFrac_dCompDens[jc];
        }
      }
    }

    // *** end of upwinding

    // populate local flux vector and derivatives
    for( localIndex ic = 0; ic < NC; ++ic )
    {
      localFlux[ic]      =  dt * compFlux[ic];
      localFlux[NC + ic] = -dt * compFlux[ic];

      for( localIndex ke = 0; ke < stencilSize; ++ke )
      {
        localIndex const localDofIndexPres = ke * NDOF;
        localFluxJacobian[ic][localDofIndexPres] = dt * dCompFlux_dP[ke][ic];
        localFluxJacobian[NC + ic][localDofIndexPres] = -dt * dCompFlux_dP[ke][ic];

        for( localIndex jc = 0; jc < NC; ++jc )
        {
          localIndex const localDofIndexComp = localDofIndexPres + jc + 1;
          localFluxJacobian[ic][localDofIndexComp] = dt * dCompFlux_dC[ke][ic][jc];
          localFluxJacobian[NC + ic][localDofIndexComp] = -dt * dCompFlux_dC[ke][ic][jc];
        }
      }
    }
  }

};

} // namespace CompositionalMultiphaseFVMKernels

} // namespace geosx


#endif //GEOSX_PHYSICSSOLVERS_FLUIDFLOW_COMPOSITIONALMULTIPHASEFVMKERNELS_HPP
