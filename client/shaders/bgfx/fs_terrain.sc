$input v_texcoord0, v_color0, v_fragpos

#include <bgfx_shader.sh>

SAMPLER2DARRAY(s_tileTextures, 0);
SAMPLER2D(s_layer1Map, 1);
SAMPLER2D(s_layer2Map, 2);
SAMPLER2D(s_alphaMap, 3);
SAMPLER2D(s_attributeMap, 4);
SAMPLER2D(s_symmetryMap, 5);
SAMPLER2D(s_lightMap, 6);
SAMPLER2D(s_shadowMap, 7);
SAMPLER2D(s_voidDistMap, 8);

// u_terrainParams: x=uTime, y=debugMode, z=luminosity, w=passType (0=terrain, 1=cliff, -1=minimap)
uniform vec4 u_terrainParams;

// Shadow mapping
uniform mat4 u_lightMtx;       // lightProj * lightView
uniform vec4 u_shadowParams;   // x=enabled(0/1), yzw=unused
// u_fogParams: x=fogNear, y=fogFar, z=fogHeightBase, w=fogHeightFade
uniform vec4 u_fogParams;
// u_fogColor: xyz=fog RGB
uniform vec4 u_fogColor;
// u_viewPos: xyz=camera position
uniform vec4 u_viewPos;

vec2 applySymmetry(vec2 uv, uint symmetry) {
    uint rot = symmetry & 3u;
    bool flipX = (symmetry & 4u) != 0u;
    bool flipY = (symmetry & 8u) != 0u;
    vec2 res = uv;
    if (flipX) res.x = 1.0 - res.x;
    if (flipY) res.y = 1.0 - res.y;
    if (rot == 1u) res = vec2(1.0 - res.y, res.x);
    else if (rot == 2u) res = vec2(1.0 - res.x, 1.0 - res.y);
    else if (rot == 3u) res = vec2(res.y, 1.0 - res.x);
    return res;
}

float computeEdgeFog(vec3 worldPos) {
    float edgeWidth = 600.0;    // Short fade band — only darken very close to edge
    float edgeMargin = 100.0;   // Start darkening almost at the edge
    float terrainMax = 25600.0;
    float dEdge = min(min(worldPos.x, terrainMax - worldPos.x),
                      min(worldPos.z, terrainMax - worldPos.z));
    return smoothstep(edgeMargin, edgeMargin + edgeWidth, dEdge);
}

vec4 sampleLayerSmooth(sampler2D layerMap, vec2 uv, vec2 uvBase) {
    float uTime = u_terrainParams.x;
    vec2 size = vec2(256.0, 256.0);
    vec2 texelSize = vec2_splat(1.0) / size;
    vec2 gridPos = uvBase - vec2_splat(0.5);
    vec2 f = fract(gridPos);
    vec2 i = floor(gridPos);

    // Center cell
    vec2 centerCoord = (floor(uvBase) + vec2_splat(0.5)) * texelSize;
    float centerSrc = texture2D(layerMap, centerCoord).r * 255.0;
    uint centerSym = uint(texture2D(s_symmetryMap, centerCoord).r * 255.0 + 0.5);

    bool centerIsWater = (abs(centerSrc - 5.0) < 0.1);

    float src0, src1, src2, src3;
    uint sym0, sym1, sym2, sym3;
    uint attr0, attr1, attr2, attr3;

    // k=0: offset (0,0)
    vec2 coord0 = (i + vec2(0.0, 0.0) + vec2_splat(0.5)) * texelSize;
    src0 = texture2D(layerMap, coord0).r * 255.0;
    sym0 = uint(texture2D(s_symmetryMap, coord0).r * 255.0 + 0.5);
    attr0 = uint(texture2D(s_attributeMap, coord0).r * 255.0 + 0.5);

    // k=1: offset (1,0)
    vec2 coord1 = (i + vec2(1.0, 0.0) + vec2_splat(0.5)) * texelSize;
    src1 = texture2D(layerMap, coord1).r * 255.0;
    sym1 = uint(texture2D(s_symmetryMap, coord1).r * 255.0 + 0.5);
    attr1 = uint(texture2D(s_attributeMap, coord1).r * 255.0 + 0.5);

    // k=2: offset (0,1)
    vec2 coord2 = (i + vec2(0.0, 1.0) + vec2_splat(0.5)) * texelSize;
    src2 = texture2D(layerMap, coord2).r * 255.0;
    sym2 = uint(texture2D(s_symmetryMap, coord2).r * 255.0 + 0.5);
    attr2 = uint(texture2D(s_attributeMap, coord2).r * 255.0 + 0.5);

    // k=3: offset (1,1)
    vec2 coord3 = (i + vec2(1.0, 1.0) + vec2_splat(0.5)) * texelSize;
    src3 = texture2D(layerMap, coord3).r * 255.0;
    sym3 = uint(texture2D(s_symmetryMap, coord3).r * 255.0 + 0.5);
    attr3 = uint(texture2D(s_attributeMap, coord3).r * 255.0 + 0.5);

    // Check if any bilinear neighbor has TW_NOGROUND (bridge area)
    bool anyNoGround = ((attr0 & 8u) != 0u) || ((attr1 & 8u) != 0u)
                    || ((attr2 & 8u) != 0u) || ((attr3 & 8u) != 0u);

    // Snap void (tile 255) neighbors to center tile to prevent garbage
    // texture bleeding at void/rift edges. Bridge cells are patched CPU-side
    // (tile 255 → nearest valid tile) so this won't trigger for them.
    if (src0 > 254.5) { src0 = centerSrc; sym0 = centerSym; }
    if (src1 > 254.5) { src1 = centerSrc; sym1 = centerSym; }
    if (src2 > 254.5) { src2 = centerSrc; sym2 = centerSym; }
    if (src3 > 254.5) { src3 = centerSrc; sym3 = centerSym; }

    // Sample tile textures for each corner
    vec4 c0, c1, c2, c3;

    // Helper macro-like inline: sample tile with water anim + symmetry + bridge snap
    // k=0
    {
        bool isWater = (abs(src0 - 5.0) < 0.1);
        if (isWater != centerIsWater && anyNoGround) {
            vec2 cUV = fract(uv * 0.25);
            cUV = applySymmetry(cUV, centerSym);
            if (centerIsWater) {
                cUV.x += uTime * 0.05;
                cUV.y += sin(uTime * 0.4 + (uv.y * 0.25) * 10.0) * 0.02;
            }
            c0 = texture2DArray(s_tileTextures, vec3(cUV, floor(centerSrc + 0.5)));
        } else {
            vec2 tileUV = fract(uv * 0.25);
            tileUV = applySymmetry(tileUV, sym0);
            if (isWater) {
                tileUV.x += uTime * 0.05;
                tileUV.y += sin(uTime * 0.4 + (uv.y * 0.25) * 10.0) * 0.02;
            }
            c0 = texture2DArray(s_tileTextures, vec3(tileUV, floor(src0 + 0.5)));
        }
    }
    // k=1
    {
        bool isWater = (abs(src1 - 5.0) < 0.1);
        if (isWater != centerIsWater && anyNoGround) {
            vec2 cUV = fract(uv * 0.25);
            cUV = applySymmetry(cUV, centerSym);
            if (centerIsWater) {
                cUV.x += uTime * 0.05;
                cUV.y += sin(uTime * 0.4 + (uv.y * 0.25) * 10.0) * 0.02;
            }
            c1 = texture2DArray(s_tileTextures, vec3(cUV, floor(centerSrc + 0.5)));
        } else {
            vec2 tileUV = fract(uv * 0.25);
            tileUV = applySymmetry(tileUV, sym1);
            if (isWater) {
                tileUV.x += uTime * 0.05;
                tileUV.y += sin(uTime * 0.4 + (uv.y * 0.25) * 10.0) * 0.02;
            }
            c1 = texture2DArray(s_tileTextures, vec3(tileUV, floor(src1 + 0.5)));
        }
    }
    // k=2
    {
        bool isWater = (abs(src2 - 5.0) < 0.1);
        if (isWater != centerIsWater && anyNoGround) {
            vec2 cUV = fract(uv * 0.25);
            cUV = applySymmetry(cUV, centerSym);
            if (centerIsWater) {
                cUV.x += uTime * 0.05;
                cUV.y += sin(uTime * 0.4 + (uv.y * 0.25) * 10.0) * 0.02;
            }
            c2 = texture2DArray(s_tileTextures, vec3(cUV, floor(centerSrc + 0.5)));
        } else {
            vec2 tileUV = fract(uv * 0.25);
            tileUV = applySymmetry(tileUV, sym2);
            if (isWater) {
                tileUV.x += uTime * 0.05;
                tileUV.y += sin(uTime * 0.4 + (uv.y * 0.25) * 10.0) * 0.02;
            }
            c2 = texture2DArray(s_tileTextures, vec3(tileUV, floor(src2 + 0.5)));
        }
    }
    // k=3
    {
        bool isWater = (abs(src3 - 5.0) < 0.1);
        if (isWater != centerIsWater && anyNoGround) {
            vec2 cUV = fract(uv * 0.25);
            cUV = applySymmetry(cUV, centerSym);
            if (centerIsWater) {
                cUV.x += uTime * 0.05;
                cUV.y += sin(uTime * 0.4 + (uv.y * 0.25) * 10.0) * 0.02;
            }
            c3 = texture2DArray(s_tileTextures, vec3(cUV, floor(centerSrc + 0.5)));
        } else {
            vec2 tileUV = fract(uv * 0.25);
            tileUV = applySymmetry(tileUV, sym3);
            if (isWater) {
                tileUV.x += uTime * 0.05;
                tileUV.y += sin(uTime * 0.4 + (uv.y * 0.25) * 10.0) * 0.02;
            }
            c3 = texture2DArray(s_tileTextures, vec3(tileUV, floor(src3 + 0.5)));
        }
    }

    // Bilinear mix
    vec4 color = mix(mix(c0, c1, f.x), mix(c2, c3, f.x), f.y);

    // Subtle water darkening so fish are visible against the water surface
    float w0 = (abs(src0 - 5.0) < 0.1) ? 1.0 : 0.0;
    float w1 = (abs(src1 - 5.0) < 0.1) ? 1.0 : 0.0;
    float w2 = (abs(src2 - 5.0) < 0.1) ? 1.0 : 0.0;
    float w3 = (abs(src3 - 5.0) < 0.1) ? 1.0 : 0.0;
    float waterFrac = mix(mix(w0, w1, f.x), mix(w2, w3, f.x), f.y);
    color.rgb *= mix(vec3_splat(1.0), vec3(0.6, 0.7, 0.5), waterFrac);

    // Mask out 255/invalid tiles
    float m0 = (src0 < 254.5) ? 1.0 : 0.0;
    float m1 = (src1 < 254.5) ? 1.0 : 0.0;
    float m2 = (src2 < 254.5) ? 1.0 : 0.0;
    float m3 = (src3 < 254.5) ? 1.0 : 0.0;
    float mask = mix(mix(m0, m1, f.x), mix(m2, m3, f.x), f.y);

    return vec4(color.rgb, mask);
}

void main()
{
    float uTime = u_terrainParams.x;
    float luminosity = u_terrainParams.z;
    vec3 fogColor = u_fogColor.xyz;
    float fogNear = u_fogParams.x;
    float fogFar = u_fogParams.y;
    float fogHeightBase = u_fogParams.z;
    float fogHeightFade = u_fogParams.w;
    vec3 viewPos = u_viewPos.xyz;

    // Minimap pass
    if (u_terrainParams.w < -0.5) {
        // Minimap: simplified rendering (no fog/shadow)
    }

    vec2 uvBase = v_texcoord0 * 256.0;

    // Smooth alpha sampling
    float alpha = texture2D(s_alphaMap, (uvBase + vec2_splat(0.5)) / 256.0).r;

    vec4 l1 = sampleLayerSmooth(s_layer1Map, uvBase, uvBase);
    vec4 l2 = sampleLayerSmooth(s_layer2Map, uvBase, uvBase);

    vec3 finalColor = mix(l1.rgb, l2.rgb, alpha * l2.a);

    // Apply lightmap and day/night luminosity
    vec4 lmVal = texture2D(s_lightMap, v_texcoord0);
    vec3 lightColor = lmVal.rgb;
    finalColor *= lightColor * v_color0.rgb * luminosity;

    // Shadow map: darken terrain in shadowed areas
    if (u_shadowParams.x > 0.5) {
        vec4 sc = mul(u_lightMtx, vec4(v_fragpos, 1.0));
        vec3 sn = sc.xyz / sc.w;
        // XY: NDC [-1,1] → UV [0,1]; Z already [0,1] from ZO projection
        sn.xy = sn.xy * 0.5 + 0.5;
        sn.y = 1.0 - sn.y; // Metal Y-flip for render targets

        if (u_shadowParams.y > 0.5) {
            float dbgDepth = texture2D(s_shadowMap, sn.xy).r;
            gl_FragColor = vec4(dbgDepth, dbgDepth, dbgDepth, 1.0);
            return;
        } else {
            // 4-tap PCF for softer shadow edges
            float texel = 1.0 / 2048.0;
            float shadow = 0.0;
            shadow += step(sn.z - 0.005, texture2D(s_shadowMap, sn.xy + vec2(-texel, -texel)).r);
            shadow += step(sn.z - 0.005, texture2D(s_shadowMap, sn.xy + vec2( texel, -texel)).r);
            shadow += step(sn.z - 0.005, texture2D(s_shadowMap, sn.xy + vec2(-texel,  texel)).r);
            shadow += step(sn.z - 0.005, texture2D(s_shadowMap, sn.xy + vec2( texel,  texel)).r);
            shadow *= 0.25;
            finalColor *= mix(0.65, 1.0, shadow);
        }
    }

    // Distance fog + height-based ground mist
    float dist = length(v_fragpos - viewPos);
    float distFog = clamp((fogFar - dist) / (fogFar - fogNear), 0.0, 1.0);
    float heightMist = 1.0 - clamp((v_fragpos.y - fogHeightBase) / fogHeightFade, 0.0, 1.0);
    float mistBuild = smoothstep(fogNear * 0.7, fogNear * 1.5, dist);
    float fogFactor = distFog * (1.0 - heightMist * mistBuild * 0.55);
    finalColor = mix(fogColor * luminosity, finalColor, fogFactor);

    // Edge fog: subtle darkening at map boundaries
    float edgeFactor = computeEdgeFog(v_fragpos);
    finalColor *= mix(0.30, 1.0, edgeFactor);

    gl_FragColor = vec4(finalColor, 1.0);
}
