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


// Python.h must be included first.
#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Source includes
#include "PyGroup.hpp"
#include "pygeosx.hpp"
#include "PyWrapper.hpp"

#define VERIFY_NON_NULL_SELF( self ) \
  PYTHON_ERROR_IF( self == nullptr, PyExc_RuntimeError, "Passed a nullptr as self.", nullptr )

#define VERIFY_INITIALIZED( self ) \
  PYTHON_ERROR_IF( self->group == nullptr, PyExc_RuntimeError, "The PyGroup is not initialized.", nullptr )

namespace geosx
{
namespace python
{

/**
 *
 */
struct PyGroup
{
  PyObject_HEAD

  static constexpr char const * docString =
  "A Python interface to geosx::dataRepository::Group.";

  dataRepository::Group * group;
};

/**
 *
 */
static PyObject * PyGroup_repr( PyObject * const obj )
{
  PyGroup const * const pyGroup = LvArray::python::convert< PyGroup >( obj, getPyGroupType() );
  if ( pyGroup == nullptr )
  { return nullptr; }

  VERIFY_INITIALIZED( pyGroup );

  std::string const path = pyGroup->group->getPath();
  std::string const type = LvArray::system::demangle( typeid( *(pyGroup->group) ).name() );
  std::string const repr = path + " ( " + type + " )";
  return PyUnicode_FromString( repr.c_str() );
}

static constexpr char const * PyGroup_groupsDocString =
"groups(self)\n"
"--\n\n"
"Return a list of the subgroups.\n"
"\n"
"Returns\n"
"_______\n"
"list of Group\n"
"    A list containing each subgroup.";
static PyObject * PyGroup_groups( PyGroup * const self, PyObject * const args )
{
  GEOSX_UNUSED_VAR( args );

  VERIFY_NON_NULL_SELF( self );
  VERIFY_INITIALIZED( self );

  localIndex const numSubGroups = self->group->numSubGroups();

  PyObject * pyList = PyList_New( numSubGroups );
  Py_ssize_t i = 0;
  bool error = false;
  self->group->forSubGroups( [pyList, &i, &error] ( dataRepository::Group & subGroup )
  {
    PyObject * const retGroup = createNewPyGroup( subGroup );
    error = error || retGroup == nullptr;

    PyList_SET_ITEM( pyList, i, retGroup );
    ++i;
  } );

  if ( error )
  {
    Py_DECREF( pyList );
    return nullptr;
  }

  return pyList;
}

static constexpr char const * PyGroup_wrappersDocString =
"wrappers(self)\n"
"--\n\n"
"Return a list of the wrappers.\n"
"\n"
"Returns\n"
"_______\n"
"list of Wrapper\n"
"    A list containing each wrapper.";
static PyObject * PyGroup_wrappers( PyGroup * const self, PyObject * const args )
{
  GEOSX_UNUSED_VAR( args );

  VERIFY_NON_NULL_SELF( self );
  VERIFY_INITIALIZED( self );

  localIndex const numWrappers = self->group->numWrappers();

  PyObject * pyList = PyList_New( numWrappers );
  Py_ssize_t i = 0;
  bool error = false;
  self->group->forWrappers( [pyList, &i, &error] ( dataRepository::WrapperBase & wrapper )
  {
    PyObject * const retWrapper = createNewPyWrapper( wrapper );
    error = error || retWrapper == nullptr;

    PyList_SET_ITEM( pyList, i, retWrapper );
    ++i;
  } );

  if ( error )
  {
    Py_DECREF( pyList );
    return nullptr;
  }

  return pyList;
}

static constexpr char const * PyGroup_getGroupDocString =
"get_group(self, path, default, /)\n"
"--\n\n"
"Return the ``Group`` at the relative path ``path``; ``default`` is optional.\n\n"
"If no group exists and ``default`` is not given, raise a ``ValueError``;\n"
"otherwise return ``default``.\n"
"\n"
"Parameters\n"
"__________\n"
"path : str\n"
"    The relative path of the group to return.\n"
"\n"
"Returns\n"
"_______\n"
"Group\n"
"    The group at the relative path.";
static PyObject * PyGroup_getGroup( PyGroup * const self, PyObject * const args )
{
  VERIFY_NON_NULL_SELF( self );
  VERIFY_INITIALIZED( self );

  PyObject * unicodePath;
  PyObject * defaultReturnValue = nullptr;
  if ( !PyArg_ParseTuple( args, "U|O", &unicodePath, &defaultReturnValue ) )
  { return nullptr; }

  LvArray::python::PyObjectRef<> asciiPath { PyUnicode_AsASCIIString( unicodePath ) };
  if ( asciiPath == nullptr )
  { return nullptr; }

  char const * const path = PyBytes_AsString( asciiPath );
  if ( path == nullptr )
  { return nullptr; }

  dataRepository::Group * const result = self->group->GetGroupByPath( path );
  if ( result == nullptr && defaultReturnValue == nullptr )
  {
    PyErr_SetString(PyExc_ValueError, ( "No Group at " + self->group->getPath() + "/" + path ).c_str() );
    return nullptr;
  }
  else if ( result == nullptr ){
    Py_INCREF( defaultReturnValue );
    return defaultReturnValue;
  }

  // Create a new Group and set the dataRepository::Group it points to.
  return createNewPyGroup( *result );
}

static constexpr char const * PyGroup_getWrapperDocString =
"get_wrapper(self, path, default, /)\n"
"--\n\n"
"Return the `Wrapper` at the relative path ``path``; ``default`` is optional.\n\n"
"If no wrapper exists and ``default`` is not given, raise a ``ValueError``;\n"
"otherwise return ``default``.\n"
"\n"
"Parameters\n"
"__________\n"
"path : str\n"
"    The relative path of the wrapper to return.\n"
"\n"
"Returns\n"
"_______\n"
"Wrapper\n"
"    The wrapper at the relative path.";
static PyObject * PyGroup_getWrapper( PyGroup * const self, PyObject * const args )
{
  VERIFY_NON_NULL_SELF( self );
  VERIFY_INITIALIZED( self );

  PyObject * unicodePath;
  PyObject * defaultReturnValue = nullptr;
  if ( !PyArg_ParseTuple( args, "U|O", &unicodePath, &defaultReturnValue ) )
  { return nullptr; }

  LvArray::python::PyObjectRef<> asciiPath { PyUnicode_AsASCIIString( unicodePath ) };
  if ( asciiPath == nullptr )
  { return nullptr; }

  char const * const path = PyBytes_AsString( asciiPath );
  if ( path == nullptr )
  { return nullptr; }

  std::string groupPath, wrapperName;
  splitPath( path, groupPath, wrapperName );

  dataRepository::Group * const group = self->group->GetGroupByPath( groupPath );

  dataRepository::WrapperBase * const result = group->getWrapperBase( wrapperName );
  if ( result == nullptr && defaultReturnValue == nullptr )
  {
    PyErr_SetString(PyExc_ValueError, ( "No Wrapper at " + self->group->getPath() + "/" + groupPath ).c_str() );
    return nullptr;
  }
  else if ( result == nullptr ){
    Py_INCREF( defaultReturnValue );
    return defaultReturnValue;
  }

  // Create a new Group and set the dataRepository::Group it points to.
  return createNewPyWrapper( *result );
}

static constexpr char const * PyGroup_registerDocString =
"register(self, callback, /)\n"
"--\n\n"
"Register a callback on the physics solver.\n\n"
"Raise TypeError if this group is not the Physics solver.\n";
static PyObject * PyGroup_register( PyGroup * const self, PyObject * const args )
{
  VERIFY_NON_NULL_SELF( self );
  VERIFY_INITIALIZED( self );

  PyObject * callback;
  if ( !PyArg_ParseTuple( args, "O", &callback ) )
  { return nullptr; }
  if ( !PyCallable_Check( callback ) ){
    PyErr_SetString( PyExc_TypeError, "callback is not callable" );
    return nullptr;
  }
  if ( self->group->registerCallback( callback, typeid( callback ) ) ){
    Py_RETURN_NONE;
  }
  PyErr_SetString(PyExc_TypeError, "Group does not contain physics solver");
  return nullptr;
}




BEGIN_ALLOW_DESIGNATED_INITIALIZERS

static PyMethodDef PyGroup_methods[] = {
  { "groups", (PyCFunction) PyGroup_groups, METH_NOARGS, PyGroup_groupsDocString },
  { "wrappers", (PyCFunction) PyGroup_wrappers, METH_NOARGS, PyGroup_wrappersDocString },
  { "get_group", (PyCFunction) PyGroup_getGroup, METH_VARARGS, PyGroup_getGroupDocString },
  { "get_wrapper", (PyCFunction) PyGroup_getWrapper, METH_VARARGS, PyGroup_getWrapperDocString },
  { "register", (PyCFunction) PyGroup_register, METH_VARARGS, PyGroup_registerDocString },
  { nullptr, nullptr, 0, nullptr } // Sentinel
};

static PyTypeObject PyGroupType = {
  PyVarObject_HEAD_INIT( nullptr, 0 )
  .tp_name = "pygeosx.Group",
  .tp_basicsize = sizeof( PyGroup ),
  .tp_itemsize = 0,
  .tp_repr = PyGroup_repr,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_doc = PyGroup::docString,
  .tp_methods = PyGroup_methods,
  .tp_new = PyType_GenericNew,
};

END_ALLOW_DESIGNATED_INITIALIZERS

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PyObject * createNewPyGroup( dataRepository::Group & group )
{
  // Create a new Group and set the dataRepository::Group it points to.
  PyObject * const ret = PyObject_CallFunction( reinterpret_cast< PyObject * >( getPyGroupType() ), "" );
  PyGroup * const retGroup = reinterpret_cast< PyGroup * >( ret );
  if ( retGroup == nullptr )
  { return nullptr; }

  retGroup->group = &group;

  return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PyTypeObject * getPyGroupType()
{ return &PyGroupType; }

} // namespace python
} // namespace geosx

