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
 * @file ConduitRestart.cpp
 */

// HACK: the fmt/fmt.hpp include needs to come before the ConduitRestart.hpp or
//       it is *possible* to get a compile error where
//       umpire::util::FixedMallocPool::Pool is used in a template in
//       axom fmt. I have no idea why this is occuring and don't have
//       the time to diagnose the issue right now.
#include <fmt/fmt.hpp>
// Source includes
#include "ConduitRestart.hpp"
#include "mpiCommunications/MpiWrapper.hpp"
#include "common/TimingMacros.hpp"
#include "common/Path.hpp"

// TPL includes
#include <conduit_relay.hpp>
#include <conduit_blueprint.hpp>

namespace geosx
{
namespace dataRepository
{

conduit::Node rootConduitNode;


std::string writeRootNode( std::string const & rootPath )
{
  std::string rootDirName, rootFileName;
  splitPath( rootPath, rootDirName, rootFileName );

  if( MpiWrapper::Comm_rank() == 0 )
  {
    conduit::Node root;
    root[ "number_of_files" ] = MpiWrapper::Comm_size();
    root[ "file_pattern" ] = rootFileName + "/rank_%07d.hdf5";

    conduit::relay::io::save( root, rootPath + ".root", "hdf5" );

    //Added for JSON output
    conduit::relay::io::save( root, rootPath + ".root.json", "json" );

    std::string cmd = "mkdir -p " + rootPath;
    int ret = std::system( cmd.c_str());
    GEOSX_WARNING_IF( ret != 0, "Failed to create directory: command '" << cmd << "' exited with code " << std::to_string( ret ) );
  }

  MpiWrapper::Barrier( MPI_COMM_GEOSX );

  // return fmt::sprintf( rootPath + "/rank_%07d.hdf5", MpiWrapper::Comm_rank() );
  return fmt::sprintf( rootPath + "/rank_%07d", MpiWrapper::Comm_rank() );
}


std::string readRootNode( std::string const & rootPath )
{
  std::string rankFilePattern;
  if( MpiWrapper::Comm_rank() == 0 )
  {
    conduit::Node node;
    conduit::relay::io::load( rootPath + ".root", "hdf5", node );

    int const nFiles = node.fetch_child( "number_of_files" ).value();
    GEOSX_ERROR_IF_NE( nFiles, MpiWrapper::Comm_size() );

    std::string const filePattern = node.fetch_child( "file_pattern" ).as_string();

    std::string rootDirName, rootFileName;
    splitPath( rootPath, rootDirName, rootFileName );

    rankFilePattern = rootDirName + "/" + filePattern;
    GEOSX_LOG_RANK_VAR( rankFilePattern );
  }

  MpiWrapper::Broadcast( rankFilePattern, 0 );
  return fmt::sprintf( rankFilePattern, MpiWrapper::Comm_rank() );
}

/* Write out a restart file. */
void writeTree( std::string const & path )
{
  GEOSX_MARK_FUNCTION;

  // GEOSX_LOG(" PRINTING OUT ROOT CONDUIT NODE BEFORE writeRootNode");
  // rootConduitNode.print();
  std::string const filePathForRank = writeRootNode( path );
  GEOSX_LOG( "Writing out restart file at " << filePathForRank );
  conduit::relay::io::save( rootConduitNode, filePathForRank + ".hdf5", "hdf5" );

  //Added
  conduit::relay::io::save( rootConduitNode, filePathForRank + ".json", "json" );

  // Testing mesh creation from restart output
  conduit::Node mesh;
  conduit::Node coords = rootConduitNode["Problem/domain/MeshBodies/mesh1/Level0/nodeManager/ReferencePosition/__values__"];
  conduit::Node connectivity = rootConduitNode["Problem/domain/MeshBodies/mesh1/Level0/ElementRegions/elementRegionsGroup/Region2/elementSubRegions/cb1/nodeList/__values__"];
 
  // create the coordinate set
  int numNodes = coords.dtype().number_of_elements() / 3;
  int y_offset = numNodes * sizeof(conduit::float64);
  int z_offset = numNodes * sizeof(conduit::float64) * 2;

  mesh["coordsets/coords/type"] = "explicit";
  mesh["coordsets/coords/values"].set(coords);
  mesh["coordsets/coords/values/x"].set_float64_ptr(
    coords.as_float64_ptr(), numNodes);
  mesh["coordsets/coords/values/y"].set_float64_ptr(
    coords.as_float64_ptr(), numNodes, y_offset );
  mesh["coordsets/coords/values/z"].set_float64_ptr(
    coords.as_float64_ptr(), numNodes, z_offset );
  
  // add the topology
  mesh["topologies/topo/type"] = "unstructured";
  mesh["topologies/topo/coordset"] = "coords";
  mesh["topologies/topo/elements/shape"] = "hex";
  mesh["topologies/topo/elements/connectivity"].set(connectivity);

  // GEOSX_LOG("number of coordinates is: " << numNodes);
  // GEOSX_LOG("coordinates contiguous:");
  // coords.print();
  // GEOSX_LOG("coordinates contiguous type is: " << coords.dtype().name());
  // GEOSX_LOG("coordinates contiguous number of elements is: " << coords.dtype().number_of_elements());
  // GEOSX_LOG("coordinates contiguous id is: " << coords.dtype().id());
  // GEOSX_LOG("coordinates contiguous is an object T/F: " << coords.dtype().is_object());
  // GEOSX_LOG("coordinates contiguous is a list T/F: " << coords.dtype().is_list());

  // conduit::float64_array arrayCoords = coords.as_float64_array();
  // GEOSX_LOG("coordinates contiguous 1st element is: " << arrayCoords[0] );
  // GEOSX_LOG("coordinates contiguous 6th element is: " << arrayCoords[5] );

  // GEOSX_LOG("\n\nx_coords is: ");
  // mesh["coordsets/coords/values/x"].print();
  // GEOSX_LOG("x_coords type is: " << mesh["coordsets/coords/values/x"].dtype().name());
  // GEOSX_LOG("y_coords is: ");
  // mesh["coordsets/coords/values/y"].print();
  // GEOSX_LOG("z_coords is: ");
  // mesh["coordsets/coords/values/z"].print();
  // GEOSX_LOG("size of x coord type is " << sizeof(mesh["coordsets/coords/values/x"].dtype()));
  // GEOSX_LOG("size of conduit's float64 is " << sizeof(conduit::float64));
  // GEOSX_LOG("size of machine's float is " << sizeof(float));
  // GEOSX_LOG("size of conduit's index_t is " << sizeof(conduit::index_t));

  conduit::Node n_info;
  // check if n conforms
  if(conduit::blueprint::verify("mesh",mesh,n_info))
  {
      GEOSX_LOG("mesh verify succeeded.");
  }
  else
  {
      GEOSX_LOG("mesh verify failed!");
  }

  conduit::relay::io_blueprint::save( mesh, "mesh.blueprint_root");
  conduit::relay::io::save( n_info, filePathForRank + "_verify_info.json", "json" );

}

void loadTree( std::string const & path )
{
  GEOSX_MARK_FUNCTION;
  std::string const filePathForRank = readRootNode( path );
  GEOSX_LOG( "Reading in restart file at " << filePathForRank );
  conduit::relay::io::load( filePathForRank, "hdf5", rootConduitNode );
}

} /* end namespace dataRepository */
} /* end namespace geosx */
