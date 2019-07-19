/*
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Copyright (c) 2019, Lawrence Livermore National Security, LLC.
 *
 * Produced at the Lawrence Livermore National Laboratory
 *
 * LLNL-CODE-746361
 *
 * All rights reserved. See COPYRIGHT for details.
 *
 * This file is part of the GEOSX Simulation Framework.
 *
 * GEOSX is a free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License (as published by the
 * Free Software Foundation) version 2.1 dated February 1999.
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/**
 * @file SolidMechanicsLagrangianSSLEKernels.hpp
 */

#pragma once

#define STORE_NODE_DATA_LOCALLY 0

#if STORE_NODE_DATA_LOCALLY
#define VELOCITY_ACCESSOR(k, a, b) v_local[ a ][ b ]
#define POSITION_ACCESSOR(k, a, b) x_local[ b ][ a ]
#else
#define VELOCITY_ACCESSOR(k, a, b) vel[ TONODESRELATION_ACCESSOR(elemsToNodes, k, a ) ][ b ]
#define POSITION_ACCESSOR(k, a, b) X[ TONODESRELATION_ACCESSOR(elemsToNodes, k, a ) ][ b ]
#endif


#include "common/DataTypes.hpp"
#include "SolidMechanicsLagrangianFEMKernels.hpp"
#include "constitutive/ConstitutiveBase.hpp"
#include "finiteElement/ElementLibrary/FiniteElementBase.h"
#include "finiteElement/FiniteElementShapeFunctionKernel.hpp"
#include "Epetra_FECrsMatrix.h"
#include "Epetra_FEVector.h"

#include "RAJA/RAJA.hpp"

#include "finiteElement/Kinematics.h"
#include "common/TimingMacros.hpp"

#include "Epetra_SerialDenseMatrix.h"
#include "Epetra_SerialDenseVector.h"

#include "KernelMacros.hpp"

namespace geosx
{

namespace SolidMechanicsLagrangianSSLEKernels
{

template< int NUM_NODES_PER_ELEM >
GEOSX_HOST_DEVICE GEOSX_FORCE_INLINE
void stressUpdate( constitutive::LinearElasticIsotropic::KernelWrapper const & constitutive,
                   localIndex const k,
                   localIndex const q,
                   arrayView2d<localIndex const> const & elemsToNodes,
#if STORE_NODE_DATA_LOCALLY
                   real64 const (&v_local)[NUM_NODES_PER_ELEM][3],
#else
                   arrayView1d<R1Tensor const> const & vel,
#endif
#if CALC_SHAPE_FUNCTION_DERIVATIVES
                   real64 const (&dNdX)[3][8],
#else
                   arrayView4d<real64 const> const & dNdX,
#endif
                   real64 const dt,
                   arrayView2d<real64> const & meanStress,
                   arrayView3d<real64> const & devStress )
{
  real64 const Gdt = constitutive.m_shearModulus[k] * dt;
  real64 const Lamedt = constitutive.m_bulkModulus[k] * dt - 2.0/3.0 * Gdt;
  real64 const Lame2Gdt = Lamedt + 2 * Gdt;

  real64 p_stress[ 6 ] = { 0 };
  #pragma unroll
  for ( localIndex a = 0; a < NUM_NODES_PER_ELEM; ++a )
  {
    real64 const v0_x_dNdXa0 = VELOCITY_ACCESSOR(k, a, 0) * DNDX_ACCESSOR(dNdX, k, q, a, 0);
    real64 const v1_x_dNdXa1 = VELOCITY_ACCESSOR(k, a, 1) * DNDX_ACCESSOR(dNdX, k, q, a, 1);
    real64 const v2_x_dNdXa2 = VELOCITY_ACCESSOR(k, a, 2) * DNDX_ACCESSOR(dNdX, k, q, a, 2);

    p_stress[ 0 ] += ( v0_x_dNdXa0 * Lame2Gdt + v1_x_dNdXa1 * Lamedt   + v2_x_dNdXa2 * Lamedt );
    p_stress[ 1 ] += ( v0_x_dNdXa0 * Lamedt   + v1_x_dNdXa1 * Lame2Gdt + v2_x_dNdXa2 * Lamedt );
    p_stress[ 2 ] += ( v0_x_dNdXa0 * Lamedt   + v1_x_dNdXa1 * Lamedt   + v2_x_dNdXa2 * Lame2Gdt );

    p_stress[ 3 ] += ( VELOCITY_ACCESSOR(k, a, 2) * DNDX_ACCESSOR(dNdX, k, q, a, 1) +
                       VELOCITY_ACCESSOR(k, a, 1) * DNDX_ACCESSOR(dNdX, k, q, a, 2) ) * Gdt;
    p_stress[ 4 ] += ( VELOCITY_ACCESSOR(k, a, 2) * DNDX_ACCESSOR(dNdX, k, q, a, 0) +
                       VELOCITY_ACCESSOR(k, a, 0) * DNDX_ACCESSOR(dNdX, k, q, a, 2) ) * Gdt;
    p_stress[ 5 ] += ( VELOCITY_ACCESSOR(k, a, 1) * DNDX_ACCESSOR(dNdX, k, q, a, 0) +
                       VELOCITY_ACCESSOR(k, a, 0) * DNDX_ACCESSOR(dNdX, k, q, a, 1) ) * Gdt;
  }

  real64 const dMeanStress = ( p_stress[ 0 ] + p_stress[ 1 ] + p_stress[ 2 ] ) / 3.0;
  MEANSTRESS_ACCESSOR(meanStress, k, q) += dMeanStress;

  p_stress[ 0 ] -= dMeanStress;
  p_stress[ 1 ] -= dMeanStress;
  p_stress[ 2 ] -= dMeanStress;

  DEVIATORSTRESS_ACCESSOR(devStress, k, q, 0) += p_stress[ 0 ];
  DEVIATORSTRESS_ACCESSOR(devStress, k, q, 2) += p_stress[ 1 ];
  DEVIATORSTRESS_ACCESSOR(devStress, k, q, 5) += p_stress[ 2 ];
  DEVIATORSTRESS_ACCESSOR(devStress, k, q, 4) += p_stress[ 3 ];
  DEVIATORSTRESS_ACCESSOR(devStress, k, q, 3) += p_stress[ 4 ];
  DEVIATORSTRESS_ACCESSOR(devStress, k, q, 1) += p_stress[ 5 ];
}


/**
 * @struct Structure to wrap templated function that implements the explicit time integration kernels.
 */
struct ExplicitKernel
{
  /**
   * @brief Launch of the element processing kernel for explicit time integration.
   * @tparam NUM_NODES_PER_ELEM The number of nodes/dof per element.
   * @tparam NUM_QUADRATURE_POINTS The number of quadrature points per element.
   * @tparam CONSTITUTIVE_TYPE the type of the constitutive relation that is being used.
   * @param A pointer to the constitutive relation that is being used.
   * @param elementList The list of elements to be processed
   * @param elemsToNodes The map from the elements to the nodes that form that element.
   * @param dNdX The derivatives of the shape functions wrt the reference configuration.
   * @param detJ The determinant of the transformation matrix (Jacobian) to the parent element.
   * @param u The nodal array of total displacements.
   * @param vel The nodal array of velocity.
   * @param acc The nodal array of force/acceleration.
   * @param meanStress The mean stress at each element quadrature point
   * @param devStress The deviator stress at each element quadrature point.
   * @param dt The timestep
   * @return The achieved timestep.
   */
  template< localIndex NUM_NODES_PER_ELEM, localIndex NUM_QUADRATURE_POINTS, typename CONSTITUTIVE_TYPE >
  static inline real64
  Launch( CONSTITUTIVE_TYPE * const constitutiveRelation,
          arrayView2d<localIndex const> const & elemsToNodes,
#if CALC_SHAPE_FUNCTION_DERIVATIVES==1
          arrayView4d<real64 const> const &,
          arrayView2d<real64 const> const &,
          arrayView1d<R1Tensor const> const & X,
#else
          arrayView4d<real64 const> const & dNdX,
          arrayView2d<real64 const> const & detJ,
          arrayView1d<R1Tensor const> const & u,
#endif
          arrayView1d<R1Tensor const> const & vel,
          arrayView1d<R1Tensor> const & acc,
          arrayView2d<real64> const & meanStress,
          arrayView3d<real64> const & devStress,
          real64 const dt )
  {
   GEOSX_MARK_FUNCTION;

#if defined(__CUDACC__)
    using KERNEL_POLICY = RAJA::cuda_exec< 64 >;
#elif defined(GEOSX_USE_OPENMP)
    using KERNEL_POLICY = RAJA::omp_parallel_for_exec;
#else
    using KERNEL_POLICY = RAJA::loop_exec;
#endif

#if STANDARD_ELEMENT_TONODESRELATION_LAYOUT
    localIndex const numElems = elemsToNodes.size(0);
#else
    localIndex const numElems = elemsToNodes.size(1);
#endif

    static bool outputMessage = true;
    if (outputMessage)
    {
      GEOS_LOG("numElems = " << numElems);

#if CALC_SHAPE_FUNCTION_DERIVATIVES
      GEOS_LOG("Calculating shape function derivatives on the fly");
#else
      GEOS_LOG("dNdX::shape = (" << dNdX.size(0) << ", " << dNdX.size(1) << ", " << dNdX.size(2) << ", " << dNdX.size(3) << ")");
      GEOS_LOG("detJ::shape = (" << detJ.size(0) << ", " << detJ.size(1) << ")");
#endif

#if STORE_NODE_DATA_LOCALLY
      GEOS_LOG("Moving node data into local arrays.");
#else
      GEOS_LOG("Not storing node data locally.");
#endif

      GEOS_LOG("meanStress::shape = (" << meanStress.size(0) << ", " << meanStress.size(1) << ")");
      GEOS_LOG("devStress::shape = (" << devStress.size(0) << ", " << devStress.size(1) << ", " << devStress.size(2)  << ")");
      GEOS_LOG("elemsToNodes::shape = (" << elemsToNodes.size(0) << ", " << elemsToNodes.size(1) << ")");
      outputMessage = false;
    }

    typename CONSTITUTIVE_TYPE::KernelWrapper const & constitutive = constitutiveRelation->createKernelWrapper();

    RAJA::forall< KERNEL_POLICY >( RAJA::TypedRangeSegment< localIndex >( 0, numElems ),
                                   GEOSX_DEVICE_LAMBDA ( localIndex const k )
    {
      real64 f_local[ NUM_NODES_PER_ELEM ][ 3 ] = {};

#if STORE_NODE_DATA_LOCALLY
      real64 v_local[ NUM_NODES_PER_ELEM ][ 3 ];
      for ( localIndex a = 0; a < NUM_NODES_PER_ELEM; ++a )
      {
        for ( int b = 0; b < 3; ++b )
        {
          v_local[ a ][ b ] = vel[ TONODESRELATION_ACCESSOR(elemsToNodes, k, a ) ][ b ];
        }
      }
#endif

#if CALC_SHAPE_FUNCTION_DERIVATIVES && STORE_NODE_DATA_LOCALLY
      real64 X_local[3][8];
      for ( localIndex a = 0; a < NUM_NODES_PER_ELEM; ++a )
      {
        for ( int b = 0; b < 3; ++b )
        {
          X_local[ b ][ a ] = X[ TONODESRELATION_ACCESSOR(elemsToNodes, k, a ) ][ b ];
        }
      }
#endif

      //Compute Quadrature
      for ( localIndex q = 0; q < NUM_QUADRATURE_POINTS; ++q )
      {
#if CALC_SHAPE_FUNCTION_DERIVATIVES
        real64 dNdX[3][8];
        real64 const detJ_k_q =
        FiniteElementShapeKernel::shapeFunctionDerivatives( k,
                                                            q,
                                                            elemsToNodes,
                                                          #if STORE_NODE_DATA_LOCALLY
                                                            X_local,
                                                          #else
                                                            X,
                                                          #endif
                                                            dNdX );
#else
        real64 const detJ_k_q = DETJ_ACCESSOR(detJ, k, q);
#endif

#if UPDATE_STRESS
        stressUpdate< NUM_NODES_PER_ELEM >( constitutive,
                                            k,
                                            q,
                                            elemsToNodes,
#if STORE_NODE_DATA_LOCALLY
                                            v_local,
#else
                                            vel,
#endif
                                            dNdX,
                                            dt,
                                            meanStress,
                                            devStress
                      );

        real64 const devStress_k_q_1 = DEVIATORSTRESS_ACCESSOR(devStress, k, q, 1);
        real64 const devStress_k_q_3 = DEVIATORSTRESS_ACCESSOR(devStress, k, q, 3);
        real64 const devStress_k_q_4 = DEVIATORSTRESS_ACCESSOR(devStress, k, q, 4);
        real64 const meanStress_k_q = MEANSTRESS_ACCESSOR(meanStress, k, q);

        #pragma unroll
        for ( localIndex a = 0; a < NUM_NODES_PER_ELEM; ++a )
        {
          f_local[ a ][ 0 ] -= ( devStress_k_q_1 * DNDX_ACCESSOR(dNdX, k, q, a, 1)
                          + devStress_k_q_3 * DNDX_ACCESSOR(dNdX, k, q, a, 2)
                          + DNDX_ACCESSOR(dNdX, k, q, a, 0) * ( DEVIATORSTRESS_ACCESSOR(devStress, k, q, 0) + meanStress_k_q ) ) * detJ_k_q;
          f_local[ a ][ 1 ] -= ( devStress_k_q_1 * DNDX_ACCESSOR(dNdX, k, q, a, 0)
                        + devStress_k_q_4 * DNDX_ACCESSOR(dNdX, k, q, a, 2)
                        + DNDX_ACCESSOR(dNdX, k, q, a, 1) * (DEVIATORSTRESS_ACCESSOR(devStress, k, q, 2) + meanStress_k_q ) ) * detJ_k_q;
          f_local[ a ][ 2 ] -= ( devStress_k_q_3 * DNDX_ACCESSOR(dNdX, k, q, a, 0)
                        + devStress_k_q_4 * DNDX_ACCESSOR(dNdX, k, q, a, 1)
                        + DNDX_ACCESSOR(dNdX, k, q, a, 2) * (DEVIATORSTRESS_ACCESSOR(devStress, k, q, 5) + meanStress_k_q ) ) * detJ_k_q;
        }

#else

        real64 c[ 6 ][ 6 ];
        constitutive.GetStiffness( k, c );
        real64 const dNdXa0 = ( DNDX_ACCESSOR(dNdX, k, q, 0, 0) + DNDX_ACCESSOR(dNdX, k, q, 1, 0) + DNDX_ACCESSOR(dNdX, k, q, 2, 0) + DNDX_ACCESSOR(dNdX, k, q, 3, 0) + DNDX_ACCESSOR(dNdX, k, q, 4, 0) + DNDX_ACCESSOR(dNdX, k, q, 5, 0) + DNDX_ACCESSOR(dNdX, k, q, 6, 0 ) + DNDX_ACCESSOR(dNdX, k, q, 7, 0 ) );
        real64 const dNdXa1 = ( DNDX_ACCESSOR(dNdX, k, q, 0, 1) + DNDX_ACCESSOR(dNdX, k, q, 1, 1) + DNDX_ACCESSOR(dNdX, k, q, 2, 1) + DNDX_ACCESSOR(dNdX, k, q, 3, 1) + DNDX_ACCESSOR(dNdX, k, q, 4, 1) + DNDX_ACCESSOR(dNdX, k, q, 5, 1) + DNDX_ACCESSOR(dNdX, k, q, 6, 1 ) + DNDX_ACCESSOR(dNdX, k, q, 7, 1 ) );
        real64 const dNdXa2 = ( DNDX_ACCESSOR(dNdX, k, q, 0, 2) + DNDX_ACCESSOR(dNdX, k, q, 1, 2) + DNDX_ACCESSOR(dNdX, k, q, 2, 2) + DNDX_ACCESSOR(dNdX, k, q, 3, 2) + DNDX_ACCESSOR(dNdX, k, q, 4, 2) + DNDX_ACCESSOR(dNdX, k, q, 5, 2) + DNDX_ACCESSOR(dNdX, k, q, 6, 2 ) + DNDX_ACCESSOR(dNdX, k, q, 7, 2 ) );
        #pragma unroll
        for( localIndex b=0 ; b< NUM_NODES_PER_ELEM ; ++b )
        {
            f_local[b][0] -= ( ( c[0][0]*dNdXa0*DNDX_ACCESSOR(dNdX, k, q, b, 0) +
                                 c[5][5]*dNdXa1*DNDX_ACCESSOR(dNdX, k, q, b, 1) +
                                 c[4][4]*dNdXa2*DNDX_ACCESSOR(dNdX, k, q, b, 2) ) * VELOCITY_ACCESSOR(k, b, 0) +
                               ( c[5][5]*dNdXa1*DNDX_ACCESSOR(dNdX, k, q, b, 0) +
                                 c[0][1]*dNdXa0*DNDX_ACCESSOR(dNdX, k, q, b, 1) ) * VELOCITY_ACCESSOR(k, b, 1) +
                               ( c[4][4]*dNdXa2*DNDX_ACCESSOR(dNdX, k, q, b, 0) +
                                 c[0][2]*dNdXa0*DNDX_ACCESSOR(dNdX, k, q, b, 2) ) * VELOCITY_ACCESSOR(k, b, 2)
                              ) * detJ_k_q;

            f_local[b][1] -= ( ( c[0][1]*dNdXa1*DNDX_ACCESSOR(dNdX, k, q, b, 0) +
                                 c[5][5]*dNdXa0*DNDX_ACCESSOR(dNdX, k, q, b, 1) ) * VELOCITY_ACCESSOR(k, b, 0) +
                               ( c[5][5]*dNdXa0*DNDX_ACCESSOR(dNdX, k, q, b, 0) +
                                 c[1][1]*dNdXa1*DNDX_ACCESSOR(dNdX, k, q, b, 1) +
                                 c[3][3]*dNdXa2*DNDX_ACCESSOR(dNdX, k, q, b, 2) ) * VELOCITY_ACCESSOR(k, b, 1) +
                               ( c[3][3]*dNdXa2*DNDX_ACCESSOR(dNdX, k, q, b, 1) +
                                 c[1][2]*dNdXa1*DNDX_ACCESSOR(dNdX, k, q, b, 2) ) * VELOCITY_ACCESSOR(k, b, 2)
                              ) * detJ_k_q;

            f_local[b][2] -= ( ( c[0][2]*dNdXa2*DNDX_ACCESSOR(dNdX, k, q, b, 0) +
                                 c[4][4]*dNdXa0*DNDX_ACCESSOR(dNdX, k, q, b, 2) ) * VELOCITY_ACCESSOR(k, b, 0) +
                               ( c[1][2]*dNdXa2*DNDX_ACCESSOR(dNdX, k, q, b, 1) +
                                 c[3][3]*dNdXa1*DNDX_ACCESSOR(dNdX, k, q, b, 2) ) * VELOCITY_ACCESSOR(k, b, 1) +
                               ( c[4][4]*dNdXa0*DNDX_ACCESSOR(dNdX, k, q, b, 0) +
                                 c[3][3]*dNdXa1*DNDX_ACCESSOR(dNdX, k, q, b, 1) +
                                 c[2][2]*dNdXa2*DNDX_ACCESSOR(dNdX, k, q, b, 2) ) * VELOCITY_ACCESSOR(k, b, 2)
                             ) * detJ_k_q;

        }
#endif
      }//quadrature loop

      #pragma unroll
      for ( localIndex a = 0; a < NUM_NODES_PER_ELEM; ++a )
      {
        #pragma unroll
        for ( int b = 0; b < 3; ++b )
        {
          RAJA::atomic::atomicAdd<RAJA::atomic::auto_atomic>( &acc[ TONODESRELATION_ACCESSOR(elemsToNodes, k, a ) ][ b ], f_local[ a ][ b ] );
        }
      }
    });

    return dt;
  }
};

/**
 * @struct Structure to wrap templated function that implements the implicit time integration kernel.
 */
struct ImplicitKernel
{
  /**
   * @brief Launch of the element processing kernel for implicit time integration.
   * @tparam NUM_NODES_PER_ELEM The number of nodes/dof per element.
   * @tparam NUM_QUADRATURE_POINTS The number of quadrature points per element.
   * @tparam CONSTITUTIVE_TYPE the type of the constitutive relation that is being used.
   * @param constitutiveRelation A pointer to the constitutive relation that is being used.
   * @param numElems The number of elements the kernel will process.
   * @param dt The timestep.
   * @param dNdX The derivatives of the shape functions wrt the reference configuration.
   * @param detJ The determinant of the transformation matrix (Jacobian) to the parent element.
   * @param fe A pointer to the finite element class used in this kernel.
   * @param elemGhostRank An array containing the values of the owning ranks for ghost elements.
   * @param elemsToNodes The map from the elements to the nodes that form that element.
   * @param globalDofNumber The map from localIndex to the globalDOF number.
   * @param disp The array of total displacements.
   * @param uhat The array of incremental displacements (displacement for this step).
   * @param vtilde The array for the velocity predictor.
   * @param uhattilde The array for the incremental displacement predictor.
   * @param density The array containing the density
   * @param fluidPressure Array containing element fluid pressure at the beginning of the step.
   * @param deltaFluidPressure Array containing the change in element fluid pressure over this step.
   * @param biotCoefficient The biotCoefficient used to calculate effective stress.
   * @param tiOption The time integration option used for the integration.
   * @param stiffnessDamping The stiffness damping coefficient for the Newmark method assuming Rayleigh damping.
   * @param massDamping The mass damping coefficient for the Newmark method assuming Rayleigh damping.
   * @param newmarkBeta The value of \beta in the Newmark update.
   * @param newmarkGamma The value of \gamma in the Newmark update.
   * @param globaldRdU  Pointer to the sparse matrix containing the derivatives of the residual wrt displacement.
   * @param globalResidual Pointer to the parallel vector containing the global residual.
   * @return The maximum nodal force contribution from all elements.
   */
  template< localIndex NUM_NODES_PER_ELEM, localIndex NUM_QUADRATURE_POINTS, typename CONSTITUTIVE_TYPE >
  static inline real64
  Launch( CONSTITUTIVE_TYPE * const constitutiveRelation,
          localIndex const numElems,
          real64 const dt,
          arrayView3d<R1Tensor const> const & dNdX,
          arrayView2d<real64 const > const& detJ,
          FiniteElementBase const * const fe,
          arrayView1d< integer const > const & elemGhostRank,
          arrayView2d< localIndex const > const & elemsToNodes,
          arrayView1d< globalIndex const > const & globalDofNumber,
          arrayView1d< R1Tensor const > const & disp,
          arrayView1d< R1Tensor const > const & uhat,
          arrayView1d< R1Tensor const > const & vtilde,
          arrayView1d< R1Tensor const > const & uhattilde,
          arrayView1d< real64 const > const & density,
          arrayView1d< real64 const > const & fluidPressure,
          arrayView1d< real64 const > const & deltaFluidPressure,
          arrayView1d< real64 const > const & biotCoefficient,
          timeIntegrationOption const tiOption,
          real64 const stiffnessDamping,
          real64 const massDamping,
          real64 const newmarkBeta,
          real64 const newmarkGamma,
          Epetra_FECrsMatrix * const globaldRdU,
          Epetra_FEVector * const globalResidual )
  {
    constexpr int dim = 3;
    Epetra_LongLongSerialDenseVector  elementLocalDofIndex   (dim*static_cast<int>(NUM_NODES_PER_ELEM));
    Epetra_SerialDenseVector     R     (dim*static_cast<int>(NUM_NODES_PER_ELEM));
    Epetra_SerialDenseMatrix     dRdU  (dim*static_cast<int>(NUM_NODES_PER_ELEM),
                                                  dim*static_cast<int>(NUM_NODES_PER_ELEM));
    Epetra_SerialDenseVector     element_dof_np1 (dim*static_cast<int>(NUM_NODES_PER_ELEM));

    Epetra_SerialDenseVector R_InertiaMassDamping(R);
    Epetra_SerialDenseMatrix dRdU_InertiaMassDamping(dRdU);
    Epetra_SerialDenseVector R_StiffnessDamping(R);
    Epetra_SerialDenseMatrix dRdU_StiffnessDamping(dRdU);

    real64 maxForce = 0;

    typename CONSTITUTIVE_TYPE::KernelWrapper const & constitutive = constitutiveRelation->createKernelWrapper();

    for( localIndex k=0 ; k<numElems ; ++k )
    {

      R1Tensor u_local[NUM_NODES_PER_ELEM];
      R1Tensor uhat_local[NUM_NODES_PER_ELEM];
      R1Tensor vtilde_local[NUM_NODES_PER_ELEM];
      R1Tensor uhattilde_local[NUM_NODES_PER_ELEM];

      dRdU.Scale(0);
      R.Scale(0);

      dRdU_InertiaMassDamping.Scale(0);
      R_InertiaMassDamping.Scale(0);
      dRdU_StiffnessDamping.Scale(0);
      R_StiffnessDamping.Scale(0);

      real64 c[6][6];
      constitutive.GetStiffness( k, c );

      if(elemGhostRank[k] < 0)
      {
        for( localIndex a=0 ; a<NUM_NODES_PER_ELEM ; ++a)
        {

          localIndex localNodeIndex = elemsToNodes[k][a];

          for( int i=0 ; i<dim ; ++i )
          {
            elementLocalDofIndex[static_cast<int>(a)*dim+i] = dim*globalDofNumber[localNodeIndex]+i;

            // TODO must add last solution estimate for this to be valid
            element_dof_np1(static_cast<int>(a)*dim+i) = disp[localNodeIndex][i];
          }
        }

        if( tiOption == timeIntegrationOption::ImplicitDynamic )
        {
          GEOS_ERROR("Option not supported");
          CopyGlobalToLocal< NUM_NODES_PER_ELEM, R1Tensor>( elemsToNodes[k],
                                      disp, uhat, vtilde, uhattilde,
                                      u_local, uhat_local, vtilde_local, uhattilde_local );
        }
        else
        {
          CopyGlobalToLocal<NUM_NODES_PER_ELEM,R1Tensor>( elemsToNodes[k], disp, uhat, u_local, uhat_local );
        }

        R2SymTensor referenceStress;
        if( !fluidPressure.empty() )
        {
          referenceStress.PlusIdentity( - biotCoefficient[0] * (fluidPressure[k] + deltaFluidPressure[k]));
        }


        R1Tensor dNdXa;
        R1Tensor dNdXb;


        for( integer q=0 ; q<NUM_QUADRATURE_POINTS ; ++q )
        {
          const realT detJq = detJ[k][q];
          std::vector<double> const & N = fe->values(q);

          for( integer a=0 ; a<NUM_NODES_PER_ELEM ; ++a )
          {
      //      realT const * const dNdXa = dNdX(q,a).Data();
            dNdXa = dNdX[k][q][a];

            for( integer b=0 ; b<NUM_NODES_PER_ELEM ; ++b )
            {
      //        realT const * const dNdXb = dNdX(q,b).Data();
              dNdXb = dNdX[k][q][b];

              dRdU(a*dim+0,b*dim+0) -= ( c[0][0]*dNdXa[0]*dNdXb[0] + c[5][5]*dNdXa[1]*dNdXb[1] + c[4][4]*dNdXa[2]*dNdXb[2] ) * detJq;
              dRdU(a*dim+0,b*dim+1) -= ( c[5][5]*dNdXa[1]*dNdXb[0] + c[0][1]*dNdXa[0]*dNdXb[1] ) * detJq;
              dRdU(a*dim+0,b*dim+2) -= ( c[4][4]*dNdXa[2]*dNdXb[0] + c[0][2]*dNdXa[0]*dNdXb[2] ) * detJq;

              dRdU(a*dim+1,b*dim+0) -= ( c[0][1]*dNdXa[1]*dNdXb[0] + c[5][5]*dNdXa[0]*dNdXb[1] ) * detJq;
              dRdU(a*dim+1,b*dim+1) -= ( c[5][5]*dNdXa[0]*dNdXb[0] + c[1][1]*dNdXa[1]*dNdXb[1] + c[3][3]*dNdXa[2]*dNdXb[2] ) * detJq;
              dRdU(a*dim+1,b*dim+2) -= ( c[3][3]*dNdXa[2]*dNdXb[1] + c[1][2]*dNdXa[1]*dNdXb[2] ) * detJq;

              dRdU(a*dim+2,b*dim+0) -= ( c[0][2]*dNdXa[2]*dNdXb[0] + c[4][4]*dNdXa[0]*dNdXb[2] ) * detJq;
              dRdU(a*dim+2,b*dim+1) -= ( c[1][2]*dNdXa[2]*dNdXb[1] + c[3][3]*dNdXa[1]*dNdXb[2] ) * detJq;
              dRdU(a*dim+2,b*dim+2) -= ( c[4][4]*dNdXa[0]*dNdXb[0] + c[3][3]*dNdXa[1]*dNdXb[1] + c[2][2]*dNdXa[2]*dNdXb[2] ) * detJq;


              if( tiOption == timeIntegrationOption::ImplicitDynamic )
              {

                real64 integrationFactor = density[k] * N[a] * N[b] * detJq;
                real64 temp1 = ( massDamping * newmarkGamma/( newmarkBeta * dt ) + 1.0 / ( newmarkBeta * dt * dt ) )* integrationFactor;

                for( int i=0 ; i<dim ; ++i )
                {
                  realT const acc = 1.0 / ( newmarkBeta * dt * dt ) * ( uhat[b][i] - uhattilde[b][i] );
                  realT const vel = vtilde[b][i] + newmarkGamma/( newmarkBeta * dt ) *( uhat[b][i] - uhattilde[b][i] );

                  dRdU_InertiaMassDamping(a*dim+i,b*dim+i) -= temp1;
                  R_InertiaMassDamping(a*dim+i) -= ( massDamping * vel + acc ) * integrationFactor;
                }
              }
            }
          }
        }



          R1Tensor temp;
          for( integer q=0 ; q<NUM_QUADRATURE_POINTS ; ++q )
          {
            const realT detJq = detJ[k][q];
            R2SymTensor stress0 = referenceStress;
            stress0 *= detJq;
            for( integer a=0 ; a<NUM_NODES_PER_ELEM ; ++a )
            {
              dNdXa = dNdX[k][q][a];

              temp.AijBj(stress0,dNdXa);
              realT maxF = temp.MaxVal();
              if( maxF > maxForce )
              {
                maxForce = maxF;
              }

              R(a*dim+0) -= temp[0];
              R(a*dim+1) -= temp[1];
              R(a*dim+2) -= temp[2];
            }
          }


      // TODO It is simpler to do this...try it.
      //  dRdU.Multiply(dof_np1,R);
        for( integer a=0 ; a<NUM_NODES_PER_ELEM ; ++a )
        {
          realT nodeForce = 0;
          for( integer b=0 ; b<NUM_NODES_PER_ELEM ; ++b )
          {
            for( int i=0 ; i<dim ; ++i )
            {
              for( int j=0 ; j<dim ; ++j )
              {
                R(a*dim+i) += dRdU(a*dim+i,b*dim+j) * u_local[b][j];
              }
            }

            if( tiOption == timeIntegrationOption::ImplicitDynamic )
            {
              for( int i=0 ; i<dim ; ++i )
              {
                for( int j=0 ; j<dim ; ++j )
                {
                  R_StiffnessDamping(a*dim+i) += stiffnessDamping * dRdU(a*dim+i,b*dim+j) * ( vtilde[b][j] + newmarkGamma/(newmarkBeta * dt)*(uhat[b][j]-uhattilde[b][j]) );
                }
              }
            }

          }

          nodeForce = std::max( std::max( R(a*dim+0), R(a*dim+1) ),  R(a*dim+2) );
          if( fabs(nodeForce) > maxForce )
          {
            maxForce = fabs(nodeForce);
          }
        }


        if( tiOption == timeIntegrationOption::ImplicitDynamic )
        {
          dRdU_StiffnessDamping = dRdU;
          dRdU_StiffnessDamping.Scale( stiffnessDamping * newmarkGamma / ( newmarkBeta * dt ) );

          dRdU += dRdU_InertiaMassDamping;
          dRdU += dRdU_StiffnessDamping;
          R    += R_InertiaMassDamping;
          R    += R_StiffnessDamping;
        }

        globaldRdU->SumIntoGlobalValues( elementLocalDofIndex,
                                    dRdU);

        globalResidual->SumIntoGlobalValues( elementLocalDofIndex,
                                  R);
      }
    }
    return maxForce;
  }
};

} // namespace SolidMechanicsLagrangianSSLEKernels

} // namespace geosx
