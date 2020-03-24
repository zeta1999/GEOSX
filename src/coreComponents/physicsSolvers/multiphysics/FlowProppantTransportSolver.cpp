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
 * @file FlowProppantTransportSolver.cpp
 *
 */


#include "FlowProppantTransportSolver.hpp"

#include "constitutive/ConstitutiveManager.hpp"
#include "managers/NumericalMethodsManager.hpp"
#include "finiteElement/Kinematics.h"
#include "managers/DomainPartition.hpp"
#include "mesh/MeshForLoopInterface.hpp"
#include "meshUtilities/ComputationalGeometry.hpp"

#include "managers/FieldSpecification/FieldSpecificationManager.hpp"

namespace geosx
{

using namespace dataRepository;
using namespace constitutive;

FlowProppantTransportSolver::FlowProppantTransportSolver( const std::string & name, Group * const parent ):
  SolverBase( name, parent ),
  m_proppantSolverName(),
  m_flowSolverName()
{
  registerWrapper( viewKeyStruct::proppantSolverNameString, &m_proppantSolverName, 0 )->
    setInputFlag( InputFlags::REQUIRED )->
    setDescription( "Name of the proppant transport solver to use in the flowProppantTransport solver" );

  registerWrapper( viewKeyStruct::flowSolverNameString, &m_flowSolverName, 0 )->
    setInputFlag( InputFlags::REQUIRED )->
    setDescription( "Name of the flow solver to use in the flowProppantTransport solver" );

}

void FlowProppantTransportSolver::RegisterDataOnMesh( dataRepository::Group * const )
{}

void FlowProppantTransportSolver::ImplicitStepSetup( real64 const & time_n,
                                                     real64 const & dt,
                                                     DomainPartition * const domain,
                                                     DofManager & GEOSX_UNUSED_PARAM( dofManager ),
                                                     ParallelMatrix & GEOSX_UNUSED_PARAM( matrix ),
                                                     ParallelVector & GEOSX_UNUSED_PARAM( rhs ),
                                                     ParallelVector & GEOSX_UNUSED_PARAM( solution ) )
{
  m_flowSolver->SetupSystem( domain,
                             m_flowSolver->getDofManager(),
                             m_flowSolver->getSystemMatrix(),
                             m_flowSolver->getSystemRhs(),
                             m_flowSolver->getSystemSolution() );
  m_proppantSolver->SetupSystem( domain,
                                 m_proppantSolver->getDofManager(),
                                 m_proppantSolver->getSystemMatrix(),
                                 m_proppantSolver->getSystemRhs(),
                                 m_proppantSolver->getSystemSolution() );


  m_flowSolver->ImplicitStepSetup( time_n, dt, domain,
                                   m_flowSolver->getDofManager(),
                                   m_flowSolver->getSystemMatrix(),
                                   m_flowSolver->getSystemRhs(),
                                   m_flowSolver->getSystemSolution() );
  m_proppantSolver->ImplicitStepSetup( time_n, dt, domain,
                                       m_proppantSolver->getDofManager(),
                                       m_proppantSolver->getSystemMatrix(),
                                       m_proppantSolver->getSystemRhs(),
                                       m_proppantSolver->getSystemSolution() );
 
  
}

void FlowProppantTransportSolver::ImplicitStepComplete( real64 const& time_n,
                                                        real64 const& dt,
                                                        DomainPartition * const domain )
{
  m_flowSolver->ImplicitStepComplete( time_n, dt, domain );
  m_proppantSolver->ImplicitStepComplete( time_n, dt, domain );
}

void FlowProppantTransportSolver::PostProcessInput()
{
  m_proppantSolver = this->getParent()->GetGroup(m_proppantSolverName)->group_cast<ProppantTransport*>();
  GEOSX_ERROR_IF( m_proppantSolver == nullptr, this->getName() << ": invalid solid solver name: " << m_proppantSolverName );
  
  m_flowSolver = this->getParent()->GetGroup(m_flowSolverName)->group_cast<SinglePhaseBase*>();
  GEOSX_ERROR_IF( m_flowSolver == nullptr, this->getName() << ": invalid solid solver name: " << m_flowSolverName );
}

void FlowProppantTransportSolver::InitializePostInitialConditions_PreSubGroups(Group * const GEOSX_UNUSED_PARAM( problemManager ))
{

}

FlowProppantTransportSolver::~FlowProppantTransportSolver()
{
  // TODO Auto-generated destructor stub
}


void FlowProppantTransportSolver::ResetStateToBeginningOfStep( DomainPartition * const domain )
{
  m_proppantSolver->ResetStateToBeginningOfStep( domain );
  m_flowSolver->ResetStateToBeginningOfStep( domain );  
}

real64 FlowProppantTransportSolver::SolverStep( real64 const & time_n,
                                                real64 const & dt,
                                                int const cycleNumber,
                                                DomainPartition * const domain )
{

  real64 dtReturn = dt;
  real64 dtReturnTemporary;

  m_proppantSolver->ResizeFractureFields(time_n, dt, domain);
  
  if(cycleNumber == 0)
  {
    FieldSpecificationManager const & boundaryConditionManager = FieldSpecificationManager::get();
    boundaryConditionManager.ApplyInitialConditions( domain );
  }

  this->ImplicitStepSetup( time_n, dt, domain, m_dofManager, m_matrix, m_rhs, m_solution );
  m_proppantSolver->PreStepUpdate(time_n, dt, cycleNumber, domain);
  
  int iter = 0;
  while( iter <  this->m_nonlinearSolverParameters.m_maxIterNewton )
  {
    if( iter == 0 )
    {
      // reset the states of all slave solvers if any of them has been reset
      ResetStateToBeginningOfStep( domain );
    }
    if( getLogLevel() >= 1 )
    {
      GEOSX_LOG_RANK_0( "\tIteration: " << iter+1  << ", FlowSolver: " );
    }

    dtReturnTemporary = m_flowSolver->NonlinearImplicitStep( time_n,
                                                             dtReturn,
                                                             cycleNumber,
                                                             domain,
                                                             m_flowSolver->getDofManager(),
                                                             m_flowSolver->getSystemMatrix(),
                                                             m_flowSolver->getSystemRhs(),
                                                             m_flowSolver->getSystemSolution() );

    if( dtReturnTemporary < dtReturn )
    {
      iter = 0;
      dtReturn = dtReturnTemporary;
      continue;
    }
  
    NonlinearSolverParameters const & fluidNonLinearParams = m_flowSolver->getNonlinearSolverParameters();
    if( fluidNonLinearParams.m_numNewtonIterations <= this->m_nonlinearSolverParameters.m_minIterNewton && iter > 0 &&
        getLogLevel() >= 1)
    {
      GEOSX_LOG_RANK_0( "***** The iterative coupling has converged in " << iter  << " iterations! *****\n" );
      break;
    }

    if( getLogLevel()  >= 1 )
    {
      GEOSX_LOG_RANK_0( "\tIteration: " << iter+1  << ", Proppant Solver: " );
    }
    
    dtReturnTemporary = m_proppantSolver->NonlinearImplicitStep( time_n,
                                                                 dtReturn,
                                                                 cycleNumber,
                                                                 domain,
                                                                 m_proppantSolver->getDofManager(),
                                                                 m_proppantSolver->getSystemMatrix(),
                                                                 m_proppantSolver->getSystemRhs(),
                                                                 m_proppantSolver->getSystemSolution() );

    if( dtReturnTemporary < dtReturn )
    {
      iter = 0;
      dtReturn = dtReturnTemporary;
      continue;
    }

    ++iter;
  }

  this->ImplicitStepComplete( time_n, dt, domain );
  m_proppantSolver->PostStepUpdate(time_n, dtReturn, cycleNumber, domain);

  
  return dtReturn;
}

REGISTER_CATALOG_ENTRY( SolverBase, FlowProppantTransportSolver, std::string const &, Group * const )

} /* namespace geosx */
