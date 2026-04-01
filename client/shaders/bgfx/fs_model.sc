$input v_texcoord0, v_normal, v_fragpos

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_shadowMap, 1);
SAMPLER2D(s_lightMap, 2);
SAMPLER2D(s_matcapTex, 3);

// Packed parameters: x=objectAlpha, y=blendMeshLight, z=chromeMode, w=chromeTime
uniform vec4 u_params;
// Packed parameters2: x=luminosity, y=decalMode(<-0.5), z=cliffFade, w=cliffTopH
uniform vec4 u_params2;
// UV offset (applied in vertex shader — needed in fragment for decal mode)
uniform vec4 u_texCoordOffset;

// Shadow mapping
uniform mat4 u_lightMtx;       // lightProj * lightView
uniform vec4 u_shadowParams;   // x=enabled(0/1), yzw=unused

uniform vec4 u_viewPos;       // xyz = camera position
uniform vec4 u_lightPos;      // xyz = sun position
uniform vec4 u_lightColor;    // xyz = sun color

// Fog: x=near, y=far, z=useFog(0/1), w=unused
uniform vec4 u_fogParams;
uniform vec4 u_fogColor;

// Terrain lightmap: xyz = lightmap color (per-object uniform fallback),
// w = useLightmapTex flag (1.0 = sample from s_lightMap per-pixel)
uniform vec4 u_terrainLight;
// Glow color for +7/+9/+11 items
uniform vec4 u_glowColor;
// Base tint for +3/+5 enhancement
uniform vec4 u_baseTint;

// Point lights: position.xyz + range in w
#define MAX_POINT_LIGHTS 64
uniform vec4 u_lightPosRange[MAX_POINT_LIGHTS];
uniform vec4 u_lightColorArr[MAX_POINT_LIGHTS];
uniform vec4 u_lightCount;  // .x = active light count

// Edge fog: darken fragments near terrain boundaries
float computeEdgeFog(vec3 worldPos) {
    float edgeWidth = 2500.0;
    float edgeMargin = 500.0;
    float terrainMax = 25600.0;
    float dEdge = min(min(worldPos.x, terrainMax - worldPos.x),
                      min(worldPos.z, terrainMax - worldPos.z));
    return smoothstep(edgeMargin, edgeMargin + edgeWidth, dEdge);
}

void main()
{
    float objectAlpha   = u_params.x;
    float blendMeshLit  = u_params.y;
    float chromeMode    = u_params.z;
    float chromeTime    = u_params.w;
    float luminosity    = u_params2.x;
    int   numLights     = int(u_lightCount.x);

    // Basic lighting (sun) — modulated by terrain lightmap
    vec3 norm = normalize(v_normal);
    vec3 lightDir = normalize(u_lightPos.xyz - v_fragpos);
    float diff = max(abs(dot(norm, lightDir)), 0.5); // Two-sided lighting
    vec3 tLight;
    if (u_terrainLight.w > 0.5) {
        // Per-pixel lightmap sampling (world objects): smooth across meshes
        vec2 lmUV = vec2(v_fragpos.z / 25600.0, v_fragpos.x / 25600.0);
        vec4 lmVal = texture2D(s_lightMap, lmUV);
        tLight = max(lmVal.rgb, vec3_splat(0.03));
        // Cliff-face lighting for void areas: use heightFade as primary
        // brightness when lightmap is dark (void cells have RGB near 0).
        float terrainH = lmVal.a;
        float heightFade = smoothstep(terrainH - 600.0, terrainH, v_fragpos.y);
        float lmBright = max(tLight.r, max(tLight.g, tLight.b));
        float voidBright = 0.35 + heightFade * 0.65;
        // Blend: dark lightmap → use voidBright; bright lightmap → keep as-is
        float voidMix = 1.0 - smoothstep(0.20, 0.45, lmBright);
        tLight = vec3_splat(mix(lmBright, voidBright, voidMix));
    } else {
        // Per-object uniform (characters/monsters/NPCs)
        tLight = max(u_terrainLight.xyz, vec3_splat(0.30));
    }
    vec3 sunLit = diff * u_lightColor.xyz * tLight;

    // Shadow map: subtly darken sun contribution in shadowed areas
    if (u_shadowParams.x > 0.5) {
        vec4 sc = mul(u_lightMtx, vec4(v_fragpos, 1.0));
        vec3 sn = sc.xyz / sc.w;
        // XY: NDC [-1,1] → UV [0,1]; Z already [0,1] from ZO projection
        sn.xy = sn.xy * 0.5 + 0.5;
        sn.y = 1.0 - sn.y; // Metal Y-flip for render targets
        // 4-tap PCF for softer shadow edges
        float texel = 1.0 / 2048.0;
        float shadow = 0.0;
        shadow += step(sn.z - 0.005, texture2D(s_shadowMap, sn.xy + vec2(-texel, -texel)).r);
        shadow += step(sn.z - 0.005, texture2D(s_shadowMap, sn.xy + vec2( texel, -texel)).r);
        shadow += step(sn.z - 0.005, texture2D(s_shadowMap, sn.xy + vec2(-texel,  texel)).r);
        shadow += step(sn.z - 0.005, texture2D(s_shadowMap, sn.xy + vec2( texel,  texel)).r);
        shadow *= 0.25;
        sunLit *= mix(0.55, 1.0, shadow);
    }

    // Specular setup
    vec3 viewDir = normalize(u_viewPos.xyz - v_fragpos);
    float roughness = 0.18;
    float a2 = roughness * roughness;

    // Point lights (diffuse + specular in single pass)
    vec3 pointLit = vec3_splat(0.0);
    float pointSpec = 0.0;
    for (int i = 0; i < numLights; ++i) {
        vec3 toLightVec = u_lightPosRange[i].xyz - v_fragpos;
        float dist = length(toLightVec);
        float range = u_lightPosRange[i].w;
        float atten = max(1.0 - dist / range, 0.0);
        atten *= atten; // Quadratic falloff
        vec3 lDir = toLightVec / max(dist, 0.001); // normalize without redundant length()
        float d = max(abs(dot(norm, lDir)), 0.25);
        pointLit += d * atten * u_lightColorArr[i].xyz;
        // GGX specular for closest 4 lights only
        if (i < 4) {
            vec3 plHalf = normalize(lDir + viewDir);
            float plNdotH = max(dot(norm, plHalf), 0.0);
            float plDenom = plNdotH * plNdotH * (a2 - 1.0) + 1.0;
            float plSpec = a2 / (3.14159 * plDenom * plDenom) * 0.08 * atten;
            pointSpec = max(pointSpec, plSpec);
        }
    }

    // GGX/Trowbridge-Reitz specular (sun)
    vec3 halfDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(norm, halfDir), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float specular = a2 / (3.14159 * denom * denom) * 0.08;

    // Fresnel rim — edges of surfaces reflect more (Schlick approximation)
    float NdotV = max(dot(norm, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 3.0) * 0.25;

    vec3 specContrib = vec3_splat((specular + pointSpec * 0.3 + fresnel) * tLight.r);
    vec3 lighting = min(sunLit + pointLit + specContrib, vec3_splat(1.6));

    // Matcap sphere-map reflection + environment gradient
    float matcapStr = step(1.5, u_baseTint.w); // 2.0=on (characters/monsters/NPCs), 0.0/1.0=off
    vec3 matcapColor = vec3_splat(0.0);
    vec3 envGradient = vec3_splat(0.0);
    if (matcapStr > 0.5) {
        vec3 viewNorm = normalize(mul(u_view, vec4(norm, 0.0)).xyz);
        vec2 matcapUV = viewNorm.xy * 0.48 + 0.5;
        matcapColor = texture2D(s_matcapTex, matcapUV).rgb;
        // Environment gradient: sky-blue top, warm-brown bottom
        vec3 skyCol = vec3(0.6, 0.7, 0.9);
        vec3 gndCol = vec3(0.15, 0.12, 0.1);
        envGradient = mix(gndCol, skyCol, norm.y * 0.5 + 0.5);
    }

    // Chrome/Metal: replace texture coords with normal-derived UVs
    vec2 finalUV = v_texcoord0;
    if (chromeMode > 0.5 && chromeMode < 1.5) {
        // RENDER_CHROME
        float wave = chromeTime * 0.1;
        finalUV.x = norm.z * 0.5 + wave;
        finalUV.y = norm.y * 0.5 + wave * 2.0;
    } else if (chromeMode > 1.5 && chromeMode < 2.5) {
        // RENDER_CHROME2
        float wave2 = mod(chromeTime, 5.0) * 0.24 - 0.4;
        finalUV.x = (norm.z + norm.x) * 0.8 + wave2 * 2.0;
        finalUV.y = (norm.y + norm.x) * 1.0 + wave2 * 3.0;
    } else if (chromeMode > 2.5 && chromeMode < 3.5) {
        // RENDER_METAL
        finalUV.x = norm.z * 0.5 + 0.2;
        finalUV.y = norm.y * 0.5 + 0.5;
    } else if (chromeMode > 3.5 && chromeMode < 4.5) {
        // RENDER_CHROME4
        float wave = chromeTime * 0.1;
        vec3 L = vec3(cos(chromeTime * 1.0), sin(chromeTime * 2.0), 1.0);
        finalUV.x = dot(norm, L);
        finalUV.y = 1.0 - dot(norm, L);
        finalUV.y = finalUV.y - norm.z * 0.5 + wave * 3.0;
        finalUV.x = finalUV.x + norm.y * 0.5 + L.y * 3.0;
    }

    vec4 texColor = texture2D(s_texColor, finalUV);
    float alphaRef = max(u_params2.y, 0.01);
    if (texColor.a < alphaRef) discard;

    // Item glow: additive enhancement pass
    vec3 glowC = u_glowColor.xyz;
    float glowSum = glowC.x + glowC.y + glowC.z;
    vec3 baseTint = u_baseTint.xyz;
    vec3 finalLight;

    if (glowSum > 0.001) {
        if (chromeMode > 1.5 && chromeMode < 2.5) {
            // CHROME2: color modulates scene light
            finalLight = glowC * lighting * luminosity;
        } else if (chromeMode > 3.5 && chromeMode < 4.5) {
            // CHROME4: color modulates scene light
            finalLight = glowC * lighting * luminosity;
        } else {
            // CHROME/METAL: fixed palette color bypasses scene lighting
            finalLight = glowC;
        }
    } else {
        // Colored specular: metals tint reflections with their own color
        // Derive "metallicness" from texture brightness (bright areas = more metallic)
        float texBright = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        float metallic = smoothstep(0.3, 0.8, texBright) * 0.3;
        vec3 specTint = mix(vec3_splat(1.0), texColor.rgb, metallic);
        vec3 coloredSpec = specContrib * specTint;

        // Matcap + env gradient blend: 70% matcap, 30% gradient
        vec3 reflContrib = mix(envGradient, matcapColor, 0.7);
        float matcapMetal = smoothstep(0.25, 0.75, texBright) * 0.15 * matcapStr;
        vec3 matcapFinal = reflContrib * mix(vec3_splat(1.0), texColor.rgb, metallic * 0.5);

        finalLight = (sunLit + pointLit) * blendMeshLit * luminosity * baseTint
                   + coloredSpec * luminosity
                   + matcapFinal * matcapMetal * luminosity;
    }

    gl_FragColor = vec4(finalLight, objectAlpha) * texColor;

    // Decal mode: brightness + edge fade to hide square polygon outline
    // Activated by setting u_params2.y < -0.5 (negative value signals decal mode)
    if (u_params2.y < -0.5) {
        // 1) Brightness fade: dark pixels (JPEG artifacts, background) become transparent
        //    Star pattern is bright (>0.15), artifacts are dim (<0.05)
        float brightness = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        float brightFade = smoothstep(0.04, 0.12, brightness);
        // 2) Square edge fade: clip outermost 2% of polygon (Chebyshev distance)
        vec2 origUV = v_texcoord0 - u_texCoordOffset.xy;
        vec2 uvFromCenter = origUV - vec2(0.5, 0.5);
        float edgeDist = max(abs(uvFromCenter.x), abs(uvFromCenter.y));
        float edgeFade = 1.0 - smoothstep(0.48, 0.50, edgeDist);
        gl_FragColor.a *= brightFade * edgeFade;
    }

    // Fog
    if (u_fogParams.z > 0.5) {
        float dist = length(v_fragpos - u_viewPos.xyz);
        float fogFactor = clamp((u_fogParams.y - dist) / (u_fogParams.y - u_fogParams.x), 0.0, 1.0);
        gl_FragColor.xyz = mix(u_fogColor.xyz * luminosity, gl_FragColor.xyz, fogFactor);

        // Edge fog
        float edgeFactor = computeEdgeFog(v_fragpos);
        float edgeBlend = mix(0.75, 1.0, edgeFactor);
        gl_FragColor.xyz = gl_FragColor.xyz * edgeBlend;
    }

    // Cliff/foundation fade: discard fragments below terrain level
    // Prevents underground geometry (foundations, cliff bases) from blocking water etc.
    float cliffFade = u_params2.z;
    if (cliffFade > 0.5) {
        float cliffTopH = u_params2.w;
        if (v_fragpos.y < cliffTopH - 20.0) {
            discard;
        }
        float fadeFactor = smoothstep(cliffTopH - 20.0, cliffTopH, v_fragpos.y);
        gl_FragColor.rgb *= fadeFactor;
    }
}
