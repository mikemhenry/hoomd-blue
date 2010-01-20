/*
Highly Optimized Object-oriented Many-particle Dynamics -- Blue Edition
(HOOMD-blue) Open Source Software License Copyright 2008, 2009 Ames Laboratory
Iowa State University and The Regents of the University of Michigan All rights
reserved.

HOOMD-blue may contain modifications ("Contributions") provided, and to which
copyright is held, by various Contributors who have granted The Regents of the
University of Michigan the right to modify and/or distribute such Contributions.

Redistribution and use of HOOMD-blue, in source and binary forms, with or
without modification, are permitted, provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions, and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
list of conditions, and the following disclaimer in the documentation and/or
other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of HOOMD-blue's
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS ``AS IS''
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND/OR
ANY WARRANTIES THAT THIS SOFTWARE IS FREE OF INFRINGEMENT ARE DISCLAIMED.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// $Id$
// $URL$
// Maintainer: phillicl

#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4103 4244 )
#endif

#include <iostream>

#include <boost/bind.hpp>
#include <boost/function.hpp>

#include "FENEBondForceCompute.h"
#include "ConstForceCompute.h"
#ifdef ENABLE_CUDA
#include "FENEBondForceComputeGPU.h"
#endif

#include "Initializers.h"

using namespace std;
using namespace boost;

/*! \file fenebond_force_test.cc
    \brief Implements unit tests for BondForceCompute and child classes
    \ingroup unit_tests
*/

//! Name the boost unit test module
#define BOOST_TEST_MODULE BondForceTests
#include "boost_utf_configure.h"

//! Typedef to make using the boost::function factory easier
typedef boost::function<shared_ptr<FENEBondForceCompute>  (shared_ptr<SystemDefinition> sysdef)> bondforce_creator;

//! Perform some simple functionality tests of any BondForceCompute
void bond_force_basic_tests(bondforce_creator bf_creator, ExecutionConfiguration exec_conf)
    {
#ifdef ENABLE_CUDA
    g_gpu_error_checking = true;
#endif
    
    /////////////////////////////////////////////////////////
    // start with the simplest possible test: 2 particles in a huge box with only one bond type
    shared_ptr<SystemDefinition> sysdef_2(new SystemDefinition(2, BoxDim(1000.0), 1, 1, 0, 0, 0,  exec_conf));
    shared_ptr<ParticleData> pdata_2 = sysdef_2->getParticleData();
    
    ParticleDataArrays arrays = pdata_2->acquireReadWrite();
    arrays.x[0] = arrays.y[0] = arrays.z[0] = 0.0;
    arrays.x[1] = Scalar(0.9);
    arrays.y[1] = arrays.z[1] = 0.0;
    pdata_2->release();
    
    // create the bond force compute to check
    shared_ptr<FENEBondForceCompute> fc_2 = bf_creator(sysdef_2);
    fc_2->setParams(0, Scalar(1.5), Scalar(1.1), Scalar(1.0), Scalar(1.0/4.0));
    
    // compute the force and check the results
    fc_2->compute(0);
    ForceDataArrays force_arrays = fc_2->acquire();
    // check that the force is correct, it should be 0 since we haven't created any bonds yet
    MY_BOOST_CHECK_SMALL(force_arrays.fx[0], tol_small);
    MY_BOOST_CHECK_SMALL(force_arrays.fy[0], tol_small);
    MY_BOOST_CHECK_SMALL(force_arrays.fz[0], tol_small);
    MY_BOOST_CHECK_SMALL(force_arrays.pe[0], tol_small);
    MY_BOOST_CHECK_SMALL(force_arrays.virial[0], tol_small);
    
    // add a bond and check again
    sysdef_2->getBondData()->addBond(Bond(0, 0, 1));
    fc_2->compute(1);
    
    // this time there should be a force
    force_arrays = fc_2->acquire();
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[0], -30.581156, tol);
    MY_BOOST_CHECK_SMALL(force_arrays.fy[0], tol_small);
    MY_BOOST_CHECK_SMALL(force_arrays.fz[0], tol_small);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[0], 1.33177578 + 0.25/2, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[0], 4.58717, tol);
    
    // check that the two forces are negatives of each other
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[0], -force_arrays.fx[1], tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fy[0], -force_arrays.fy[1], tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fz[0], -force_arrays.fz[1], tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[0], force_arrays.pe[1], tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[1], 4.58717, tol);
    
    // rearrange the two particles in memory and see if they are properly updated
    arrays = pdata_2->acquireReadWrite();
    arrays.x[0] = Scalar(0.9);
    arrays.x[1] = Scalar(0.0);
    arrays.tag[0] = 1;
    arrays.tag[1] = 0;
    arrays.rtag[0] = 1;
    arrays.rtag[1] = 0;
    pdata_2->release();
    
    // notify that we made the sort
    pdata_2->notifyParticleSort();
    // recompute at the same timestep, the forces should still be updated
    fc_2->compute(1);
    
    // this time there should be a force
    force_arrays = fc_2->acquire();
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[0], 30.581156, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[1], -30.581156, tol);
    
    ////////////////////////////////////////////////////////////////////
    // now, lets do a more thorough test and include boundary conditions
    // there are way too many permutations to test here, so I will simply
    // test +x, -x, +y, -y, +z, and -z independantly
    // build a 6 particle system with particles across each boundary
    // also test more than one type of bond
    shared_ptr<SystemDefinition> sysdef_6(new SystemDefinition(6, BoxDim(20.0, 40.0, 60.0), 1, 3, 0, 0, 0, exec_conf));
    shared_ptr<ParticleData> pdata_6 = sysdef_6->getParticleData();
    
    arrays = pdata_6->acquireReadWrite();
    arrays.x[0] = Scalar(-9.6); arrays.y[0] = 0; arrays.z[0] = 0.0;
    arrays.x[1] =  Scalar(9.6); arrays.y[1] = 0; arrays.z[1] = 0.0;
    arrays.x[2] = 0; arrays.y[2] = Scalar(-19.6); arrays.z[2] = 0.0;
    arrays.x[3] = 0; arrays.y[3] = Scalar(19.6); arrays.z[3] = 0.0;
    arrays.x[4] = 0; arrays.y[4] = 0; arrays.z[4] = Scalar(-29.6);
    arrays.x[5] = 0; arrays.y[5] = 0; arrays.z[5] =  Scalar(29.6);
    pdata_6->release();
    
    shared_ptr<FENEBondForceCompute> fc_6 = bf_creator(sysdef_6);
    fc_6->setParams(0, Scalar(1.5), Scalar(1.1), Scalar(1.0), Scalar(1.0/4.0));
    fc_6->setParams(1, Scalar(2.0*1.5), Scalar(1.1), Scalar(1.0), Scalar(1.0/4.0));
    fc_6->setParams(2, Scalar(1.5), Scalar(1.0), Scalar(1.0), Scalar(1.0/4.0));
    
    sysdef_6->getBondData()->addBond(Bond(0, 0,1));
    sysdef_6->getBondData()->addBond(Bond(1, 2,3));
    sysdef_6->getBondData()->addBond(Bond(2, 4,5));
    
    fc_6->compute(0);
    // check that the forces are correctly computed
    force_arrays = fc_6->acquire();
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[0], 187.121131, tol);
    MY_BOOST_CHECK_SMALL(force_arrays.fy[0], tol_small);
    MY_BOOST_CHECK_SMALL(force_arrays.fz[0], tol_small);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[0], 5.71016443 + 0.25/2, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[0], 24.9495, tol);
    
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[1], -187.121131, tol);
    MY_BOOST_CHECK_SMALL(force_arrays.fy[1], tol_small);
    MY_BOOST_CHECK_SMALL(force_arrays.fz[1], tol_small);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[1], 5.71016443 + 0.25/2, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[1], 24.9495, tol);
    
    MY_BOOST_CHECK_SMALL(force_arrays.fx[2], tol_small);
    MY_BOOST_CHECK_CLOSE(force_arrays.fy[2], 184.573762, tol);
    MY_BOOST_CHECK_SMALL(force_arrays.fz[2], tol_small);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[2],  6.05171988 + 0.25/2, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[2], 24.6098, tol);
    
    MY_BOOST_CHECK_SMALL(force_arrays.fx[3], tol_small);
    MY_BOOST_CHECK_CLOSE(force_arrays.fy[3], -184.573762, tol);
    MY_BOOST_CHECK_SMALL(force_arrays.fz[3], tol_small);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[3], 6.05171988 + 0.25/2, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[3], 24.6098, tol);
    
    MY_BOOST_CHECK_SMALL(force_arrays.fx[4], tol_small);
    MY_BOOST_CHECK_SMALL(force_arrays.fy[4], tol_small);
    MY_BOOST_CHECK_CLOSE(force_arrays.fz[4], 186.335166, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[4], 5.7517282 + 0.25/2, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[4], 24.8447, tol);
    
    MY_BOOST_CHECK_SMALL(force_arrays.fx[5], tol_small);
    MY_BOOST_CHECK_SMALL(force_arrays.fy[5], tol_small);
    MY_BOOST_CHECK_CLOSE(force_arrays.fz[5], -186.335166, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[5],  5.7517282 + 0.25/2, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[5], 24.8447, tol);
    
    // one more test: this one will test two things:
    // 1) That the forces are computed correctly even if the particles are rearranged in memory
    // and 2) That two forces can add to the same particle
    shared_ptr<SystemDefinition> sysdef_4(new SystemDefinition(4, BoxDim(100.0, 100.0, 100.0), 1, 1, 0, 0, 0, exec_conf));
    shared_ptr<ParticleData> pdata_4 = sysdef_4->getParticleData();
    
    arrays = pdata_4->acquireReadWrite();
    // make a square of particles
    arrays.x[0] = 0.0; arrays.y[0] = 0.0; arrays.z[0] = 0.0;
    arrays.x[1] = 1.0; arrays.y[1] = 0; arrays.z[1] = 0.0;
    arrays.x[2] = 0; arrays.y[2] = 1.0; arrays.z[2] = 0.0;
    arrays.x[3] = 1.0; arrays.y[3] = 1.0; arrays.z[3] = 0.0;
    
    arrays.tag[0] = 2;
    arrays.tag[1] = 3;
    arrays.tag[2] = 0;
    arrays.tag[3] = 1;
    arrays.rtag[arrays.tag[0]] = 0;
    arrays.rtag[arrays.tag[1]] = 1;
    arrays.rtag[arrays.tag[2]] = 2;
    arrays.rtag[arrays.tag[3]] = 3;
    pdata_4->release();
    
    // build the bond force compute and try it out
    shared_ptr<FENEBondForceCompute> fc_4 = bf_creator(sysdef_4);
    fc_4->setParams(0, Scalar(1.5), Scalar(1.75), Scalar(1.2), Scalar(1.0/4.0));
    // only add bonds on the left, top, and bottom of the square
    sysdef_4->getBondData()->addBond(Bond(0, 2,3));
    sysdef_4->getBondData()->addBond(Bond(0, 2,0));
    sysdef_4->getBondData()->addBond(Bond(0, 0,1));
    
    fc_4->compute(0);
    force_arrays = fc_4->acquire();
    // the right two particles should only have a force pulling them left
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[1], 86.85002865, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fy[1], 0, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fz[1], 0, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[1], 7.08810039/2.0, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[1], 14.475, tol);
    
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[3], 86.85002865, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fy[3], 0, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fz[3], 0, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[3], 7.08810039/2.0, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[3], 14.475, tol);
    
    // the bottom left particle should have a force pulling up and to the right
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[0], -86.850028653, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fy[0], -86.85002865, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fz[0], 0, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[0], 7.08810039, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[0], 14.475*2.0, tol);
    
    // and the top left particle should have a force pulling down and to the right
    MY_BOOST_CHECK_CLOSE(force_arrays.fx[2], -86.85002865, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fy[2], 86.85002865, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.fz[2], 0, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.pe[2], 7.08810039, tol);
    MY_BOOST_CHECK_CLOSE(force_arrays.virial[2], 14.475*2.0, tol);
    }

//! Compares the output of two FENEBondForceComputes
void bond_force_comparison_tests(bondforce_creator bf_creator1,
                                 bondforce_creator bf_creator2,
                                 ExecutionConfiguration exec_conf)
    {
#ifdef ENABLE_CUDA
    g_gpu_error_checking = true;
#endif
    
    const unsigned int M = 10;
    const unsigned int N = M*M*M;
    
    // create a particle system to sum forces on
    // use a simple cubic array of particles so that random bonds
    // don't result in huge forces on a random particle arrangement
    SimpleCubicInitializer sc_init(M, 1.5, "A");
    shared_ptr<SystemDefinition> sysdef(new SystemDefinition(sc_init, exec_conf));
    shared_ptr<ParticleData> pdata = sysdef->getParticleData();
    
    shared_ptr<FENEBondForceCompute> fc1 = bf_creator1(sysdef);
    shared_ptr<FENEBondForceCompute> fc2 = bf_creator2(sysdef);
    fc1->setParams(0, Scalar(300.0), Scalar(1.6), Scalar(1.0), Scalar(1.0/4.0));
    fc2->setParams(0, Scalar(300.0), Scalar(1.6), Scalar(1.0), Scalar(1.0/4.0));
    
    // displace particles a little so all forces aren't alike
    ParticleDataArrays arrays = pdata->acquireReadWrite();
    BoxDim box = pdata->getBox();
    for (unsigned int i = 0; i < N; i++)
        {
        arrays.x[i] += Scalar((rand())/Scalar(RAND_MAX) - 0.5) * Scalar(0.01);
        if (arrays.x[i] < box.xlo)
            arrays.x[i] = box.xlo;
        if (arrays.x[i] > box.xhi)
            arrays.x[i] = box.xhi;
            
        arrays.y[i] += Scalar((rand())/Scalar(RAND_MAX) - 0.5) * Scalar(0.05);
        if (arrays.y[i] < box.ylo)
            arrays.y[i] = box.ylo;
        if (arrays.y[i] > box.yhi)
            arrays.y[i] = box.yhi;
            
        arrays.z[i] += Scalar((rand())/Scalar(RAND_MAX) - 0.5) * Scalar(0.001);
        if (arrays.z[i] < box.zlo)
            arrays.z[i] = box.zlo;
        if (arrays.z[i] > box.zhi)
            arrays.z[i] = box.zhi;
        }
    pdata->release();
    
    // add bonds
    for (unsigned int i = 0; i < M; i++)
        for (unsigned int j = 0; j < M; j++)
            for (unsigned int k = 0; k < M-1; k++)
                {
                sysdef->getBondData()->addBond(Bond(0, i*M*M + j*M + k, i*M*M + j*M + k + 1));
                }
                
                
    // compute the forces
    fc1->compute(0);
    fc2->compute(0);
    
    // verify that the forces are identical (within roundoff errors)
    ForceDataArrays arrays1 = fc1->acquire();
    ForceDataArrays arrays2 = fc2->acquire();
    
    // compare average deviation between the two computes
    double deltaf2 = 0.0;
    double deltape2 = 0.0;
    double deltav2 = 0.0;
        
    for (unsigned int i = 0; i < N; i++)
        {
        deltaf2 += double(arrays1.fx[i] - arrays2.fx[i]) * double(arrays1.fx[i] - arrays2.fx[i]);
        deltaf2 += double(arrays1.fy[i] - arrays2.fy[i]) * double(arrays1.fy[i] - arrays2.fy[i]);
        deltaf2 += double(arrays1.fz[i] - arrays2.fz[i]) * double(arrays1.fz[i] - arrays2.fz[i]);
        deltape2 += double(arrays1.pe[i] - arrays2.pe[i]) * double(arrays1.pe[i] - arrays2.pe[i]);
        deltav2 += double(arrays1.virial[i] - arrays2.virial[i]) * double(arrays1.virial[i] - arrays2.virial[i]);

        // also check that each individual calculation is somewhat close
        BOOST_CHECK_CLOSE(arrays1.fx[i], arrays2.fx[i], loose_tol);
        BOOST_CHECK_CLOSE(arrays1.fy[i], arrays2.fy[i], loose_tol);
        BOOST_CHECK_CLOSE(arrays1.fz[i], arrays2.fz[i], loose_tol);
        BOOST_CHECK_CLOSE(arrays1.pe[i], arrays2.pe[i], loose_tol);
        BOOST_CHECK_CLOSE(arrays1.virial[i], arrays2.virial[i], loose_tol);
        }
    deltaf2 /= double(pdata->getN());
    deltape2 /= double(pdata->getN());
    deltav2 /= double(pdata->getN());
    BOOST_CHECK_SMALL(deltaf2, double(tol_small));
    BOOST_CHECK_SMALL(deltape2, double(tol_small));
    BOOST_CHECK_SMALL(deltav2, double(tol_small));
    }

//! FEBEBondForceCompute creator for bond_force_basic_tests()
shared_ptr<FENEBondForceCompute> base_class_bf_creator(shared_ptr<SystemDefinition> sysdef)
    {
    return shared_ptr<FENEBondForceCompute>(new FENEBondForceCompute(sysdef));
    }

#ifdef ENABLE_CUDA
//! FENEBondForceCompute creator for bond_force_basic_tests()
shared_ptr<FENEBondForceCompute> gpu_bf_creator(shared_ptr<SystemDefinition> sysdef)
    {
    return shared_ptr<FENEBondForceCompute>(new FENEBondForceComputeGPU(sysdef));
    }
#endif

//! boost test case for bond forces on the CPU
BOOST_AUTO_TEST_CASE( FENEBondForceCompute_basic )
    {
    bondforce_creator bf_creator = bind(base_class_bf_creator, _1);
    bond_force_basic_tests(bf_creator, ExecutionConfiguration(ExecutionConfiguration::CPU));
    }

#ifdef ENABLE_CUDA
//! boost test case for bond forces on the GPU
BOOST_AUTO_TEST_CASE( FENEBondForceComputeGPU_basic )
    {
    bondforce_creator bf_creator = bind(gpu_bf_creator, _1);
    bond_force_basic_tests(bf_creator, ExecutionConfiguration(ExecutionConfiguration::GPU));
    }

//! boost test case for comparing bond GPU and CPU BondForceComputes
BOOST_AUTO_TEST_CASE( FENEBondForceComputeGPU_compare )
    {
    bondforce_creator bf_creator_gpu = bind(gpu_bf_creator, _1);
    bondforce_creator bf_creator = bind(base_class_bf_creator, _1);
    bond_force_comparison_tests(bf_creator, bf_creator_gpu, ExecutionConfiguration(ExecutionConfiguration::GPU));
    }

#endif

#ifdef WIN32
#pragma warning( pop )
#endif
