**************************************
Unit Testing
**************************************

Unit testing is integral to the GEOSX development process. While not all components lend themselves to unit testing (for example a physics solver) every effort should be made to write comprehensive quality unit tests where appropriate.

.. include:: ../../../../coreComponents/LvArray/docs/sphinx/testing.rst
   :start-after: SPHINX_BEGIN_BUILD_TESTS
   :end-before: SPHINX_END_BUILD_TESTS

Structure
---------
Each sub-directory in ``coreComponents`` should have a ``unitTests`` directory containing the test sources. Each test consists of a ``cpp`` file whose name begins with ``test`` followed by a name to describe the test.

.. include:: ../../../../coreComponents/LvArray/docs/sphinx/testing.rst
   :start-after: SPHINX_BEGIN_ADDING_A_TEST
   :end-before: SPHINX_END_ADDING_A_TEST

GEOSX Specific Tips
-------------------


.. include:: ../../../../coreComponents/LvArray/docs/sphinx/testing.rst
   :start-after: SPHINX_BEGIN_LINKS
   :end-before: SPHINX_END_LINKS

