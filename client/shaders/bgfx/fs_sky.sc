$input v_texcoord0, v_alpha, v_worldpos

#include <bgfx_shader.sh>

SAMPLER2D(s_skyTexture, 0);

// u_skyParams: x=luminosity, y/z/w=unused
uniform vec4 u_skyParams;
// u_fogColor: xyz=fog RGB
uniform vec4 u_skyFogColor;

// Edge fog: darken sky near terrain boundaries
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
    float luminosity = u_skyParams.x;
    vec3 fogCol = u_skyFogColor.xyz;

    vec3 resultColor;
    float resultAlpha;

    if (v_alpha > 1.5) {
        // Bottom cap: solid fog color to fill void below terrain
        resultColor = fogCol * luminosity;
        resultAlpha = 1.0;
    } else {
        // Cylinder band: sky texture blended with fog, fading out at top
        vec4 texColor = texture2D(s_skyTexture, v_texcoord0);
        resultColor = mix(fogCol, texColor.xyz, 0.8) * luminosity;
        resultAlpha = v_alpha;
    }

    // Edge fog: darken at map boundaries
    float edgeFactor = computeEdgeFog(v_worldpos);
    float edgeBlend = mix(0.75, 1.0, edgeFactor);
    resultColor = resultColor * edgeBlend;

    gl_FragColor = vec4(resultColor, resultAlpha);
}
