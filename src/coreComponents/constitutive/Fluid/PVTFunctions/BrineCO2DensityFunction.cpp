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
 * @file BrineCO2DensityFunction.cpp
 */

#include "constitutive/Fluid/PVTFunctions/BrineCO2DensityFunction.hpp"

using namespace std;

namespace geosx
{

using namespace stringutilities;

namespace PVTProps
{

BrineCO2DensityFunction::BrineCO2DensityFunction( string_array const & inputPara,
                                                  string_array const & componentNames,
                                                  real64_array const & componentMolarWeight):
  PVTFunctionBase( inputPara[1], componentNames, componentMolarWeight)
{
  bool notFound = 1;

  for(localIndex i = 0; i < componentNames.size(); ++i)
  {

    if(streq(componentNames[i], "CO2") || streq(componentNames[i], "co2"))
    {
      m_CO2Index = i;
      notFound = 0;
      break;
    }

  }

  GEOS_ERROR_IF(notFound, "Component CO2 is not found!");

  notFound = 1;

  for(localIndex i = 0; i < componentNames.size(); ++i)
  {

    if(streq(componentNames[i], "Water") || streq(componentNames[i], "water"))
    {
      m_waterIndex = i;
      notFound = 0;
      break;
    }

  }

  GEOS_ERROR_IF(notFound, "Component Water/Brine is not found!");


  MakeTable(inputPara);
}

void BrineCO2DensityFunction::MakeTable(string_array const & inputPara)
{
  real64_vector pressures;
  real64_vector temperatures;

  real64 PStart, PEnd, dP;
  real64 TStart, TEnd, dT;
  real64 P, T, m;

  PStart = stod(inputPara[2]);
  PEnd = stod(inputPara[3]);
  dP = stod(inputPara[4]);

  TStart = stod(inputPara[5]);
  TEnd = stod(inputPara[6]);
  dT = stod(inputPara[7]);

  m = stod(inputPara[8]);

  P = PStart;

  while(P <= PEnd)
  {

    pressures.push_back(P);
    P += dP;

  }

  T = TStart;

  while(T <= TEnd)
  {

    temperatures.push_back(T);
    T += dT;

  }

  unsigned long nP = pressures.size();
  unsigned long nT = temperatures.size();

  array1dT<real64_vector> densities(nP);
  for(unsigned long i = 0; i < nP; ++i)
  {
    densities[i].resize(nT);
  }


  CalculateBrineDensity(pressures, temperatures, m, densities);

  m_BrineDensityTable = make_shared<XYTable>("BrineDensityTable", pressures, temperatures, densities);
}


void BrineCO2DensityFunction::Evaluation(const EvalVarArgs& pressure, const EvalVarArgs& temperature, const array1dT<EvalVarArgs>& phaseComposition, EvalVarArgs& value, bool useMass) const
{
  EvalArgs2D P, T, density;
  P.m_var = pressure.m_var;
  P.m_der[0] = 1.0;

  T.m_var = temperature.m_var;
  T.m_der[1] = 1.0;

  density = m_BrineDensityTable->Value(P, T);

  constexpr real64 a = 37.51;
  constexpr real64 b = -9.585e-2;
  constexpr real64 c = 8.740e-4;
  constexpr real64 d = -5.044e-7;

  real64 temp = T.m_var;

  real64 V = (a + b * temp + c * temp * temp + d * temp * temp * temp) * 1e-6;

  real64 CO2MW = m_componentMolarWeight[m_CO2Index];
  real64 waterMW = m_componentMolarWeight[m_waterIndex];

  EvalVarArgs den, C, X;

  den.m_var = density.m_var;
  den.m_der[0] = density.m_der[0];

  X = phaseComposition[m_CO2Index];

  C = X * den / (waterMW * (1.0 - X));

  if(useMass)
  {
    value = den + CO2MW * C - C * den * V;
  }
  else
  {
    value = den / waterMW + C - C * den * V / waterMW;
  }
}

void BrineCO2DensityFunction::CalculateBrineDensity(const real64_vector& pressure, const real64_vector& temperature, const real64& salinity, array1dT<real64_vector>& density)
{
  constexpr real64 c1 = -9.9595;
  constexpr real64 c2 = 7.0845;  
  constexpr real64 c3 = 3.9093;

  constexpr real64 a1 = -0.004539;
  constexpr real64 a2 = -0.0001638;
  constexpr real64 a3 = 0.00002551;

  constexpr real64 AA = -3.033405;
  constexpr real64 BB = 10.128163;
  constexpr real64 CC = -8.750567;
  constexpr real64 DD = 2.663107;

  real64 P, x;

  for(unsigned long i = 0; i < pressure.size(); ++i)
  {

    P = pressure[i] / 1e5;

    for(unsigned long j = 0; j < temperature.size(); ++j)    
    {
      x = c1 * exp(a1 * salinity) + c2 * exp(a2 * temperature[j]) + c3 * exp(a3 * P);

      density[i][j] = (AA + BB * x + CC * x * x + DD * x * x * x) * 1000.0;
    }
  }
}
  
REGISTER_CATALOG_ENTRY( PVTFunctionBase,
                        BrineCO2DensityFunction,
                        string_array const &, string_array const &, real64_array const & )

} // namespace PVTProps
} // namespace geosx