#include <managers/DomainPartition.hpp>
#include <source/MeshDataWriters/MeshParts.hpp>
#include <source/Mesh/MeshFactory.hpp>

#include "TestTarget.hpp"

namespace geosx{

using namespace dataRepository;

TestTarget::TestTarget( const std::string & name, Group * const parent ):
  OutputBase( name, parent )
{
  registerWrapper( "meshFile", &m_meshFile )->
    setInputFlag( InputFlags::REQUIRED )->
    setDescription( "Mesh file containing the data." );

  registerWrapper( "scaler", &m_scale )->
    setApplyDefaultValue( 1.0 )->
    setInputFlag( InputFlags::OPTIONAL )->
    setDescription( "Field scaler" );
}

void TestTarget::Execute( real64 const time_n,
                          real64 const GEOSX_UNUSED_PARAM( dt ),
                          integer const GEOSX_UNUSED_PARAM( cycleNumber ),
                          integer const GEOSX_UNUSED_PARAM( eventCounter ),
                          real64 const GEOSX_UNUSED_PARAM( eventProgress ),
                          dataRepository::Group * domain )
{
  GEOSX_LOG_RANK( "Executing TestTarget" );
  const std::map<int, std::string> timeToPressureFieldName{
    { 0, "P2006" },
    { 1, "P2007" },
    { 2, "P2008" },
    { 3, "P2008" },
    { 4, "P2010" },
    { 5, "P2011" },
    { 6, "P2012" },
    { 7, "P2013" },
    { 8, "P2014" },
    { 9, "P2015" },
    { 10, "P2016" },
    { 11, "P2016" },
    { 12, "P2018" },
    { 13, "P2019" }
  };

  const unsigned int nSecondsPerYear( 60 * 60 * 24 * 365 );
//  const unsigned int year( 2006 + time_n / nSecondsPerYear );
//  GEOSX_LOG("year = " + std::to_string(year));
//  const std::string fieldName( "P" + std::to_string( year ) );
  GEOSX_LOG_RANK("time_n = " + std::to_string(time_n));
//  const std::string fieldName( "P" + std::to_string( 2006 + time_n ) );
//  const std::string fieldName( "P" );
  const std::string fieldName(timeToPressureFieldName.at(int(time_n)));
  GEOSX_LOG_RANK("fieldName = " + fieldName );

  DomainPartition * d = domain->group_cast< DomainPartition * >();
  MeshLevel * meshLevel = d->getMeshBody( 0 )->getMeshLevel( 0 );
  ElementRegionManager * elemMgr = meshLevel->getElemManager();
  ElementRegionBase * region = elemMgr->GetRegion( 0 );
  ElementSubRegionBase * subRegion = region->GetSubRegion( 0 );
  auto & pressure = subRegion->getWrapper< real64_array >( "pressure" )->reference();
  const arrayView2d< real64, 1 > & elementCenters = subRegion->getElementCenter();

  arrayView1d <globalIndex> const & l2g = subRegion->localToGlobalMap();

  PAMELA::Mesh * mesh = GetMesh();
  auto pc = mesh->get_PolyhedronCollection();
  PAMELA::Property< PAMELA::PolyhedronCollection, double > * properties = mesh->get_PolyhedronProperty_double();

  std::unordered_map< string, PAMELA::ParallelEnsemble< double>> & nameToField = properties->get_PropertyMap();
  auto const & inputPressureItr = nameToField.find( fieldName );
  GEOSX_ERROR_IF( inputPressureItr == nameToField.end(), "Could not find field " + fieldName);
  PAMELA::ParallelEnsemble< double > & inputPressure = inputPressureItr->second;

  for( int cellIndex = 0; cellIndex < pressure.size(); cellIndex++ )
  {
    pressure[cellIndex] = m_scale * inputPressure[l2g[cellIndex]];
  }
  GEOSX_LOG_RANK( "TestTarget done" );

//  auto & bulkModulus = subRegion->getWrapper< real64_array >( "rock_BulkModulus" )->reference();
//  for( int cellIndex = 0; cellIndex < bulkModulus.size(); cellIndex++ )
//  {
//    bulkModulus[cellIndex] *= 2 ;
//  }
//
//  auto & shearModulus = subRegion->getWrapper< real64_array >( "rock_ShearModulus" )->reference();
//  for( int cellIndex = 0; cellIndex < shearModulus.size(); cellIndex++ )
//  {
//    shearModulus[cellIndex] *= 2 ;
//  }
}

PAMELA::Mesh * TestTarget::GetMesh()
{
  if( m_mesh == nullptr )
  {
    m_mesh = PAMELA::MeshFactory::makeMesh( m_meshFile );
  }
  return m_mesh;
}

REGISTER_CATALOG_ENTRY( OutputBase, TestTarget, std::string const &, Group * const )

}