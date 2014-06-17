//#include "stdafx.h"
#include <tbb/tbb.h>
#include <algorithm>
#include "mpTypes.h"

mpWorld g_mpWorld;


const int32 SIMD_LANES = 8;

template<class T>
T clamp(T v, T minv, T maxv)
{
    return std::min<T>(std::max<T>(v, minv), maxv);
}

int32 soa_blocks(int32 i)
{
    return (i/SIMD_LANES) + (i%SIMD_LANES>0 ? 1 : 0);
}

template<class T>
inline void simd_store(void *address, T v)
{
    _mm_store_ps((float*)address, (const simdvec4&)v);
}

void mpSoAnize( int32 num, const mpParticle *particles, ispc::Particle_SOA8 *out )
{
    int32 blocks = soa_blocks(num);
    for(int32 bi=0; bi<blocks; ++bi) {
        int32 i = SIMD_LANES*bi;
        ist::soavec34 soav;
        soav = ist::soa_transpose34(particles[i+0].position, particles[i+1].position, particles[i+2].position, particles[i+3].position);
        simd_store(out[bi].x+0, soav.x());
        simd_store(out[bi].y+0, soav.y());
        simd_store(out[bi].z+0, soav.z());
        soav = ist::soa_transpose34(particles[i+0].velocity, particles[i+1].velocity, particles[i+2].velocity, particles[i+3].velocity);
        simd_store(out[bi].vx+0, soav.x());
        simd_store(out[bi].vy+0, soav.y());
        simd_store(out[bi].vz+0, soav.z());

        soav = ist::soa_transpose34(particles[i+4].position, particles[i+5].position, particles[i+6].position, particles[i+7].position);
        simd_store(out[bi].x+4, soav.x());
        simd_store(out[bi].y+4, soav.y());
        simd_store(out[bi].z+4, soav.z());
        soav = ist::soa_transpose34(particles[i+4].velocity, particles[i+5].velocity, particles[i+6].velocity, particles[i+7].velocity);
        simd_store(out[bi].vx+4, soav.x());
        simd_store(out[bi].vy+4, soav.y());
        simd_store(out[bi].vz+4, soav.z());

        simd_store(out[bi].hit+0, _mm_set1_epi32(-1));
        simd_store(out[bi].hit+4, _mm_set1_epi32(-1));

        //// �s�v
        //soas = ist::simdvec4_set(particles[i+0].params.density, particles[i+1].params.density, particles[i+2].params.density, particles[i+3].params.density);
        //_mm_store_ps(out[bi].density+0, soas);
        //soas = ist::simdvec4_set(particles[i+4].params.density, particles[i+5].params.density, particles[i+6].params.density, particles[i+7].params.density);
        //_mm_store_ps(out[bi].density+4, soas);
    }
}

void mpAoSnize( int32 num, const ispc::Particle_SOA8 *particles, mpParticle *out )
{
    int32 blocks = soa_blocks(num);
    for(int32 bi=0; bi<blocks; ++bi) {
        int32 i = 8*bi;
        ist::soavec44 aos_pos[2] = {
            ist::soa_transpose44(
                _mm_load_ps(particles[bi].x + 0),
                _mm_load_ps(particles[bi].y + 0),
                _mm_load_ps(particles[bi].z + 0),
                _mm_set1_ps(1.0f) ),
            ist::soa_transpose44(
                _mm_load_ps(particles[bi].x + 4),
                _mm_load_ps(particles[bi].y + 4),
                _mm_load_ps(particles[bi].z + 4),
                _mm_set1_ps(1.0f) ),
        };
        ist::soavec44 aos_vel[2] = {
            ist::soa_transpose44(
                _mm_load_ps(particles[bi].vx + 0),
                _mm_load_ps(particles[bi].vy + 0),
                _mm_load_ps(particles[bi].vz + 0),
                _mm_load_ps(particles[bi].speed + 0)),
            ist::soa_transpose44(
                _mm_load_ps(particles[bi].vx + 4),
                _mm_load_ps(particles[bi].vy + 4),
                _mm_load_ps(particles[bi].vz + 4),
                _mm_load_ps(particles[bi].speed + 4)),
        };

        int32 e = std::min<int32>(SIMD_LANES, num-i);
        for(int32 ei=0; ei<e; ++ei) {
            out[i+ei].position = aos_pos[ei/4][ei%4];
            out[i+ei].velocity = aos_vel[ei/4][ei%4];
            out[i+ei].params.density = particles[bi].density[ei];
            out[i+ei].params.hit_prev = out[i + ei].params.hit;
            out[i+ei].params.hit = particles[bi].hit[ei];
        }
    }
}

inline uint32 mpGenHash(const mpParticle &particle)
{
    mpKernelParams &p = g_mpWorld.m_params;
    mpTempParams &t = g_mpWorld.m_tmp;
    XMFLOAT3 &bl = (XMFLOAT3&)g_mpWorld.m_tmp.WorldBBBL;
    XMFLOAT3 &rcpCell = (XMFLOAT3&)g_mpWorld.m_tmp.RcpCellSize;
    XMFLOAT3 &ppos = (XMFLOAT3&)particle.position;

    uint32 r =  clamp<int32>(int32((ppos.x-bl.x)*rcpCell.x), 0, p.WorldDiv_x-1) |
               (clamp<int32>(int32((ppos.z-bl.z)*rcpCell.z), 0, p.WorldDiv_z-1) << (t.WorldDivBits_x)) |
               (clamp<int32>(int32((ppos.y-bl.y)*rcpCell.y), 0, p.WorldDiv_y-1) << (t.WorldDivBits_x+t.WorldDivBits_z)) ;

    if(particle.params.lifetime<=0.0f) { r |= 0x80000000; }
    return r;
}

inline void mpGenIndex(uint32 hash, int32 &xi, int32 &yi, int32 &zi)
{
    mpKernelParams &p = g_mpWorld.m_params;
    mpTempParams &t = g_mpWorld.m_tmp;
    xi =  hash & (p.WorldDiv_x - 1);
    zi = (hash >> (t.WorldDivBits_x)) & (p.WorldDiv_z - 1);
    yi = (hash >> (t.WorldDivBits_x+t.WorldDivBits_z)) & (p.WorldDiv_y - 1);
}


mpWorld::mpWorld()
    : m_num_active_particles(0)
    , m_renderer(nullptr)
{
    collision_spheres.reserve(64);

    particles.resize(m_params.MaxParticles);
    clearParticles();
}

void mpWorld::clearParticles()
{
    for (uint32 i = 0; i < particles.size(); ++i) {
        particles[i].params.lifetime = 0.0f;
    }
}

void mpWorld::clearCollidersAndForces()
{
    collision_spheres.clear();
    collision_planes.clear();
    collision_boxes.clear();
    forces.clear();
}

void mpWorld::addParticles(mpParticle *p, uint32_t num)
{
    num = std::min<uint32_t>(num, m_params.MaxParticles - m_num_active_particles);
    for (uint32_t i = 0; i<num; ++i) {
        particles[m_num_active_particles+i] = p[i];
        particles[m_num_active_particles+i].params.lifetime = m_params.LifeTime;
    }
    m_num_active_particles += num;
}

void mpWorld::update(float32 dt)
{
    mpKernelParams &p = m_params;
    mpTempParams &t = m_tmp;
    int cell_num = p.WorldDiv_x*p.WorldDiv_y*p.WorldDiv_z;
    int particles_par_task = 2048;
    int cells_par_task = 256;

    {
        XMFLOAT3 &wpos = (XMFLOAT3&)p.WorldCenter_x;
        XMFLOAT3 &wsize = (XMFLOAT3&)p.WorldSize_x;
        XMFLOAT3 &rcpCell = (XMFLOAT3&)t.RcpCellSize;
        XMFLOAT3 &bl = (XMFLOAT3&)t.WorldBBBL;
        XMFLOAT3 &ur = (XMFLOAT3&)t.WorldBBUR;
        p.ParticleSize = std::max<float>(p.ParticleSize, 0.00001f);
        p.MaxParticles = std::max<int>(p.MaxParticles, 128);
        m_num_active_particles = std::min<int>(m_num_active_particles, p.MaxParticles);
        p.WorldDiv_x = std::max<int>(p.WorldDiv_x, 1);
        p.WorldDiv_y = std::max<int>(p.WorldDiv_y, 1);
        p.WorldDiv_z = std::max<int>(p.WorldDiv_z, 1);
        t.WorldDivBits_x = mpMSB(p.WorldDiv_x);
        t.WorldDivBits_y = mpMSB(p.WorldDiv_y);
        t.WorldDivBits_z = mpMSB(p.WorldDiv_z);
        rcpCell = XMFLOAT3(1.0f, 1.0f, 1.0f) / (wsize*2.0f / XMFLOAT3(p.WorldDiv_x, p.WorldDiv_y, p.WorldDiv_z));
        bl = wpos - wsize;
        ur = wpos + wsize;

        int SOADataNum = std::max<int>(cell_num, p.MaxParticles / 8);
        cell.resize(cell_num);
        particles.resize(p.MaxParticles);
        particles_soa.resize(SOADataNum);
        particles_imd.resize(SOADataNum);
    }

    sphGridData *ce = &cell[0]; 
    ispc::Force *fs = forces.empty() ? nullptr : &forces[0];
    ispc::SphereCollider  *point_c = collision_spheres.empty() ? nullptr : &collision_spheres[0];
    ispc::PlaneCollider   *plane_c = collision_planes.empty() ? nullptr : &collision_planes[0];
    ispc::BoxCollider     *box_c = collision_boxes.empty() ? nullptr : &collision_boxes[0];

    ispc::sphUpdateConstants(m_params);

    // clear grid
    tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
        [&](const tbb::blocked_range<int> &r) {
            for(int i=r.begin(); i!=r.end(); ++i) {
                ce[i].begin = ce[i].end = 0;
            }
        });

    // gen hash
    tbb::parallel_for(tbb::blocked_range<int>(0, (int32)m_num_active_particles, particles_par_task),
        [&](const tbb::blocked_range<int> &r) {
            for(int i=r.begin(); i!=r.end(); ++i) {
                XMFLOAT3 &ppos = (XMFLOAT3&)particles[i].position;
                XMFLOAT3 &bl = g_mpWorld.m_tmp.WorldBBBL;
                XMFLOAT3 &ur = g_mpWorld.m_tmp.WorldBBUR;
                if (ppos.x<bl.x || ppos.y<bl.y || ppos.z<bl.z ||
                    ppos.x>ur.x || ppos.y>ur.y || ppos.z>ur.z)
                {
                    particles[i].params.lifetime = std::min<float>(particles[i].params.lifetime, 20.0f);
                }
                particles[i].params.lifetime = std::max<float32>(particles[i].params.lifetime - dt, 0.0f);
                particles[i].params.hash = mpGenHash(particles[i]);
            }
        });

    // �p�[�e�B�N���� hash �� sort
    tbb::parallel_sort(&particles[0], &particles[0]+m_num_active_particles, 
        [&](const mpParticle &a, const mpParticle &b) { return a.params.hash < b.params.hash; } );

    // �p�[�e�B�N�����ǂ� grid �ɓ����Ă��邩���Z�o
    tbb::parallel_for(tbb::blocked_range<int>(0, (int32)m_num_active_particles, particles_par_task),
        [&](const tbb::blocked_range<int> &r) {
            for(int i=r.begin(); i!=r.end(); ++i) {
                const uint32 G_ID = i;
                uint32 G_ID_PREV = G_ID-1;
                uint32 G_ID_NEXT = G_ID+1;

                uint32 cell = particles[G_ID].params.hash;
                uint32 cell_prev = (G_ID_PREV==-1) ? -1 : particles[G_ID_PREV].params.hash;
                uint32 cell_next = (G_ID_NEXT == p.MaxParticles) ? -2 : particles[G_ID_NEXT].params.hash;
                if((cell & 0x80000000) != 0) { // �ŏ�� bit �������Ă����玀��ł��鈵��
                    if((cell_prev & 0x80000000) == 0) { // 
                        m_num_active_particles = G_ID;
                    }
                }
                else {
                    if(cell != cell_prev) {
                        ce[cell].begin = G_ID;
                    }
                    if(cell != cell_next) {
                        ce[cell].end = G_ID + 1;
                    }
                }
            }
    });
    {
        if( (particles[0].params.hash & 0x80000000) != 0 ) {
            m_num_active_particles = 0;
        }
        else if ((particles[p.MaxParticles - 1].params.hash & 0x80000000) == 0) {
            m_num_active_particles = p.MaxParticles;
        }
    }

    {
        int32 soai = 0;
        for(int i=0; i!=cell_num; ++i) {
            ce[i].soai = soai;
            soai += soa_blocks(ce[i].end-ce[i].begin);
        }
    }

    // AoS -> SoA
    tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
        [&](const tbb::blocked_range<int> &r) {
            for(int i=r.begin(); i!=r.end(); ++i) {
                int32 n = ce[i].end - ce[i].begin;
                if(n == 0) { continue; }
                mpParticle *p = &particles[ce[i].begin];
                ispc::Particle_SOA8 *t = &particles_soa[ce[i].soai];
                mpSoAnize(n, p, t);
            }
    });

    if (m_params.SolverType == mpSolver_Impulse) {
        // impulse
        tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
            [&](const tbb::blocked_range<int> &r) {
            for (int i = r.begin(); i != r.end(); ++i) {
                int32 n = ce[i].end - ce[i].begin;
                if (n == 0) { continue; }
                int xi, yi, zi;
                mpGenIndex(i, xi, yi, zi);
                ispc::impUpdateVelocity(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi);
            }
        });
        tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
            [&](const tbb::blocked_range<int> &r) {
            for (int i = r.begin(); i != r.end(); ++i) {
                int32 n = ce[i].end - ce[i].begin;
                if (n == 0) { continue; }
                int xi, yi, zi;
                mpGenIndex(i, xi, yi, zi);
                ispc::sphProcessExternalForce(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi,
                    fs, (int32)forces.size());
                ispc::sphProcessCollision(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi,
                    point_c, (int32)collision_spheres.size(),
                    plane_c, (int32)collision_planes.size(),
                    box_c, (int32)collision_boxes.size());
                ispc::impIntegrate(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi);
            }
        });
    }
    else if (m_params.SolverType == mpSolver_SPH || m_params.SolverType == mpSolver_SPHEst) {
        if (m_params.SolverType == mpSolver_SPH) {
            tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
                [&](const tbb::blocked_range<int> &r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    int32 n = ce[i].end - ce[i].begin;
                    if (n == 0) { continue; }
                    int xi, yi, zi;
                    mpGenIndex(i, xi, yi, zi);
                    ispc::sphUpdateDensity(
                        (ispc::Particle*)&particles_soa[0],
                        (ispc::ParticleIMData*)&particles_imd[0],
                        ce, xi, yi, zi);
                }
            });
        }
        else if (m_params.SolverType == mpSolver_SPHEst) {
            tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
                [&](const tbb::blocked_range<int> &r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    int32 n = ce[i].end - ce[i].begin;
                    if (n == 0) { continue; }
                    int xi, yi, zi;
                    mpGenIndex(i, xi, yi, zi);
                    ispc::sphUpdateDensityEst1(
                        (ispc::Particle*)&particles_soa[0],
                        (ispc::ParticleIMData*)&particles_imd[0],
                        ce, xi, yi, zi);
                }
            });
            tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
                [&](const tbb::blocked_range<int> &r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    int32 n = ce[i].end - ce[i].begin;
                    if (n == 0) { continue; }
                    int xi, yi, zi;
                    mpGenIndex(i, xi, yi, zi);
                    ispc::sphUpdateDensityEst2(
                        (ispc::Particle*)&particles_soa[0],
                        (ispc::ParticleIMData*)&particles_imd[0],
                        ce, xi, yi, zi);
                }
            });
        }

        tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
            [&](const tbb::blocked_range<int> &r) {
            for (int i = r.begin(); i != r.end(); ++i) {
                int32 n = ce[i].end - ce[i].begin;
                if (n == 0) { continue; }
                int xi, yi, zi;
                mpGenIndex(i, xi, yi, zi);
                ispc::sphUpdateForce(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi);
            }
        });
        tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
            [&](const tbb::blocked_range<int> &r) {
            for (int i = r.begin(); i != r.end(); ++i) {
                int32 n = ce[i].end - ce[i].begin;
                if (n == 0) { continue; }
                int xi, yi, zi;
                mpGenIndex(i, xi, yi, zi);
                ispc::sphProcessExternalForce(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi,
                   fs, (int32)forces.size());
                ispc::sphProcessCollision(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi,
                    &collision_spheres[0], (int32)collision_spheres.size(),
                    &collision_planes[0], (int32)collision_planes.size(),
                    &collision_boxes[0], (int32)collision_boxes.size());
                ispc::sphIntegrate(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi);
            }
        });
    }
    else if (m_params.SolverType==mpSolver_NoInteraction) {
        tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
            [&](const tbb::blocked_range<int> &r) {
            for (int i = r.begin(); i != r.end(); ++i) {
                int32 n = ce[i].end - ce[i].begin;
                if (n == 0) { continue; }
                int xi, yi, zi;
                mpGenIndex(i, xi, yi, zi);
                ispc::impZeroAccel(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi);
                ispc::sphProcessExternalForce(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi,
                    fs, (int32)forces.size());
                ispc::sphProcessCollision(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi,
                    point_c, (int32)collision_spheres.size(),
                    plane_c, (int32)collision_planes.size(),
                    box_c, (int32)collision_boxes.size());
                ispc::impIntegrate(
                    (ispc::Particle*)&particles_soa[0],
                    (ispc::ParticleIMData*)&particles_imd[0],
                    ce, xi, yi, zi);
            }
        });
    }


    // SoA -> AoS
    tbb::parallel_for(tbb::blocked_range<int>(0, cell_num, cells_par_task),
        [&](const tbb::blocked_range<int> &r) {
            for(int i=r.begin(); i!=r.end(); ++i) {
                int32 n = ce[i].end - ce[i].begin;
                if(n > 0) {
                    mpParticle *p = &particles[ce[i].begin];
                    ispc::Particle_SOA8 *t = &particles_soa[ce[i].soai];
                    mpAoSnize(n, t, p);
                }
            }
    });
}
