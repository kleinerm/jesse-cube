/*
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * Fragment shader for cube demo
 */
#version 400
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (binding = 1) uniform sampler2D tex;

layout (location = 0) in vec4 texcoord;
layout (location = 0) out vec4 uFragColor;

/* Simple pass-through fragment shader on the Vulkan side. */
void main() {
   uFragColor = texture(tex, texcoord.xy);
}

/* Unused: Would do the ST-2084 PQ Perceptual Quantizer HDR-10 mapping OETF. */
void pq_eotf_main() {
   vec3 L, Lp, f, v;

   /* Get source color sample */
   uFragColor = texture(tex, texcoord.xy);

   /* Normalize input range [0 - 10000.0 nits] to [0.0 - 1.0]; */
   L = uFragColor.rgb;
   L = L / 10000.0;

   /* Apply ST 2084 PQ OETF */
   Lp = pow(L, vec3(0.1593017578125));
   f = (0.8359375 + 18.8515625 * Lp) / (1 + 18.6875 * Lp);
   v  = pow(f, vec3(78.84375));

   /* Debug range check: If red input value greater than some nits, color it red */
   if (uFragColor.r >= 1000.0)
        v = vec3(1.0, 0.0, 0.0);

   /* Assign PQ mapped to output */
   uFragColor.rgb = v;
}
