﻿Shader "GPUParticle/Lambert" {

Properties {
    _MainTex ("Base (RGB)", 2D) = "white" {}
    _Color ("Color", Color) = (0.8, 0.8, 0.8, 1.0)
    _Emission ("Emission", Color) = (0.0, 0.0, 0.0, 0.0)
    g_size ("Particle Size", Float) = 0.2
    g_fade_time ("Fade Time", Float) = 0.3
    g_spin ("Spin", Float) = 0.0
}
SubShader {
    Tags { "RenderType"="Opaque" }

CGPROGRAM
#pragma surface surf Lambert vertex:vert addshadow

#define MPGP_SURFACE
#include "MPGPSurface.cginc" 
ENDCG
}

FallBack Off
}
