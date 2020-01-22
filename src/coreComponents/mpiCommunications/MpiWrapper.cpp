/*
 *~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * Copyright (c) 2018, Lawrence Livermore National Security, LLC.
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
 * @file MpiWrapper.cpp
 */

#include "MpiWrapper.hpp"

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif


namespace geosx
{

//int MpiWrapper::Bcast( void * buffer, int count, MPI_Datatype datatype, int root, MPI_Comm comm )
//{
//#ifdef GEOSX_USE_MPI
//  return MPI_Bcast( buffer, count, datatype, root, comm );
//#else
//  return 0;
//#endif
//
//}



int MpiWrapper::Cart_coords( MPI_Comm comm, int rank, int maxdims, int coords[] )
{
#ifdef GEOSX_USE_MPI
  return MPI_Cart_coords( comm, rank, maxdims, coords );
#else
  return 0;
#endif
}

int MpiWrapper::Cart_create( MPI_Comm comm_old, int ndims, const int dims[], const int periods[],
                             int reorder, MPI_Comm * comm_cart )
{
#ifdef GEOSX_USE_MPI
  return MPI_Cart_create( comm_old, ndims, dims, periods, reorder, comm_cart );
#else
  return 0;
#endif
}

int MpiWrapper::Cart_rank( MPI_Comm comm, const int coords[] )
{
  int rank = 0;
#ifdef GEOSX_USE_MPI
  MPI_Cart_rank( comm, coords, &rank );
#endif
  return rank;
}

int MpiWrapper::Comm_free( MPI_Comm * comm )
{
#ifdef GEOSX_USE_MPI
  return MPI_Comm_free( comm );
#else
  return 0;
#endif
}


int MpiWrapper::Finalize()
{
#ifdef GEOSX_USE_MPI
  return MPI_Finalize();
#else
  return 0;
#endif
}

std::size_t MpiWrapper::getSizeofMpiType( MPI_Datatype const type )
{
  if( type == MPI_CHAR )
  {
    return sizeof(char);
  }
  else if( type == MPI_FLOAT )
  {
    return sizeof(float);
  }
  else if( type == MPI_DOUBLE )
  {
    return sizeof(double);
  }
  else if( type == MPI_INT )
  {
    return sizeof(int);
  }
  else if( type == MPI_LONG )
  {
    return sizeof(long int);
  }
  else if( type == MPI_LONG_LONG )
  {
    return sizeof(long long int);
  }
  else
  {
      GEOSX_ERROR("No conversion implemented for MPI_Datatype "<<type);
  }
  return 0;
}


int MpiWrapper::Init( int * argc, char * * * argv )
{
#ifdef GEOSX_USE_MPI
  return MPI_Init( argc, argv );
#else
  return 0;
#endif
}

int MpiWrapper::Wait( MPI_Request * request, MPI_Status * status )
{
#ifdef GEOSX_USE_MPI
  return MPI_Wait( request, status );
#endif
  return 0;
}

int MpiWrapper::Waitany( int count, MPI_Request array_of_requests[], int * indx, MPI_Status * status )
{
#ifdef GEOSX_USE_MPI
  return MPI_Waitany( count, array_of_requests, indx, status );
#endif
  return 0;
}

int MpiWrapper::Waitall( int count, MPI_Request array_of_requests[], MPI_Status array_of_statuses[] )
{
#ifdef GEOSX_USE_MPI
  return MPI_Waitall( count, array_of_requests, array_of_statuses );
#endif
  return 0;
}

double MpiWrapper::Wtime( void )
{
#ifdef GEOSX_USE_MPI
  return MPI_Wtime( );
#else
  return 0;
#endif

}


} /* namespace geosx */

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif