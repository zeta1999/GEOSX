#ifndef GEOSX_TESTTARGET_HPP
#define GEOSX_TESTTARGET_HPP

#include <source/Mesh/Mesh.hpp>
#include "managers/Outputs/OutputBase.hpp"
#include "dataRepository/ExecutableGroup.hpp"

namespace geosx {

class TestTarget: public OutputBase
{
public:
  TestTarget( const std::string & name, Group * const parent );

  ~TestTarget(){delete m_mesh;}

  static string CatalogName() { return "TestTarget"; }

  virtual void Execute( real64 const time_n,
                        real64 const dt,
                        integer const cycleNumber,
                        integer const eventCounter,
                        real64 const eventProgress,
                        dataRepository::Group * domain ) override;

private:

  PAMELA::Mesh * GetMesh() ;

  string m_meshFile; // FIXME Do not store
  real64 m_scale;
  PAMELA::Mesh * m_mesh = nullptr;
};


} // end of geosx namespace

#endif //GEOSX_TESTTARGET_HPP
