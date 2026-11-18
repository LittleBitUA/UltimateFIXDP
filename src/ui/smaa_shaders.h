// The three pixel shaders of our SMAA 1x pass (ui/smaa.cpp). Our own implementation of the published technique
// (Jimenez et al., "SMAA: Enhanced Subpixel Morphological Antialiasing", 2012); the tables are made by ../smaa_lut.h,
// which also explains the terms (run, crossing edge, own / other side).
//
// Each shader is three string pieces: the declarations (HLSL only), the body and the entry point (HLSL only). The
// bodies are written in the part of HLSL that is also valid C++ (no swizzles except .xy and .xyz, no out parameters,
// no vector conditions), so tests/test_smaa.cpp compiles these very texts on the host, runs them on test pictures and
// checks the result: nobody can run the game where this code is written. Keep them that way, and keep the names clear
// of HLSL keywords and intrinsics (line, point, sample, cross...).
// No other headers: a plain preprocessor file.
#pragma once

// ---- 1. Edges: red = an edge on the left boundary of the pixel, green = on its top boundary ------------------------
#define UFX_SMAA_EDGE_DECL \
    "sampler sColor : register(s0);\n" /* the picture, point filter */ \
    "float4 c0 : register(c0);\n"      /* xy = texel size */ \
    "float4 c1 : register(c1);\n"      /* x = threshold, y = 1 to compare colours instead of brightness */

#define UFX_SMAA_EDGE_BODY \
    "float4 SmaaTap(float2 uv, float dx, float dy) {\n" \
    "  return tex2Dlod(sColor, float4(uv + float2(dx, dy) * c0.xy, 0.0, 0.0));\n" \
    "}\n" \
    /* how different two pixels are: by brightness, or by the channel that differs most */ \
    "float SmaaDelta(float4 a, float4 b) {\n" \
    "  float3 d = abs(a.xyz - b.xyz);\n" \
    "  float byLuma = abs(dot(a.xyz - b.xyz, float3(0.2126, 0.7152, 0.0722)));\n" \
    "  float byColor = max(d.x, max(d.y, d.z));\n" \
    "  return lerp(byLuma, byColor, c1.y);\n" \
    "}\n" \
    "float4 SmaaEdges(float2 uv) {\n" \
    "  float4 cC = SmaaTap(uv, 0.0, 0.0);\n" \
    "  float4 cL = SmaaTap(uv, -1.0, 0.0);\n" \
    "  float4 cT = SmaaTap(uv, 0.0, -1.0);\n" \
    "  float dL = SmaaDelta(cC, cL);\n" \
    "  float dT = SmaaDelta(cC, cT);\n" \
    /* local contrast adaptation: next to an edge more than twice as strong a weak one is texture, not a border */ \
    "  float dR = SmaaDelta(cC, SmaaTap(uv, 1.0, 0.0));\n" \
    "  float dB = SmaaDelta(cC, SmaaTap(uv, 0.0, 1.0));\n" \
    "  float dLL = SmaaDelta(cL, SmaaTap(uv, -2.0, 0.0));\n" \
    "  float dTT = SmaaDelta(cT, SmaaTap(uv, 0.0, -2.0));\n" \
    "  float strongest = max(max(dL, dT), max(max(dR, dB), max(dLL, dTT)));\n" \
    "  float eL = step(c1.x, dL) * step(strongest, dL * 2.0);\n" \
    "  float eT = step(c1.x, dT) * step(strongest, dT * 2.0);\n" \
    "  return float4(eL, eT, 0.0, 0.0);\n" \
    "}\n"

#define UFX_SMAA_EDGE_MAIN "float4 main(float2 uv : TEXCOORD0) : COLOR { return SmaaEdges(uv); }\n"

// ---- 2. Weights: for every edge pixel, how much of the two pixels at the edge the true border cuts off -------------
// Output: red / green = areaOther / areaOwn of the run along the top boundary, blue / alpha = the same for the run
// along the left boundary. SMAA_STEPS (texture fetches per direction, two pixels each) is defined in front of the text.
#define UFX_SMAA_WEIGHT_DECL \
    "#define SMAA_BRANCH [branch]\n" \
    "sampler sEdges : register(s0);\n"  /* linear filter */ \
    "sampler sArea : register(s1);\n"   /* point filter */ \
    "sampler sSearch : register(s2);\n" /* point filter */ \
    "float4 c0 : register(c0);\n"       /* xy = texel size */ \
    "float4 c1 : register(c1);\n"       /* x = how much a real corner loses (0 = corners are not looked for) */

#define UFX_SMAA_WEIGHT_BODY \
    /* How many pixels the run goes on behind the pixel at uv (left for axis 1,0; up for axis 0,1). Every fetch reads
       two pixels of the run and the crossing edges at them (the weights are in smaa_lut.h): 0.8281 lies between "both
       run marks" (28/32) and the most without one of them (25/32), 0.0156 is half of the smallest crossing mark. */ \
    "float SmaaSearchBack(float2 uv, float2 axis) {\n" \
    "  float2 across = float2(axis.y, axis.x);\n" \
    "  float2 stepUv = axis * c0.xy;\n" \
    "  float2 p = uv - 0.25 * stepUv - 0.125 * across * c0.xy;\n" \
    "  float crossing = 0.0;\n" \
    "  float along = 1.0;\n" \
    "  float n = -1.0;\n" \
    "  for (int i = 0; i < SMAA_STEPS; i++) {\n" \
    "    float4 e = tex2Dlod(sEdges, float4(p, 0.0, 0.0));\n" \
    "    crossing = dot(e.xy, axis);\n" \
    "    along = dot(e.xy, across);\n" \
    "    if (along < 0.8281 || crossing > 0.0156) break;\n" \
    "    p = p - 2.0 * stepUv;\n" \
    "    n = n + 2.0;\n" \
    "  }\n" \
    "  float4 t = tex2Dlod(sSearch, float4((float2(crossing, along) * 32.0 + 0.5) / 64.0, 0.0, 0.0));\n" \
    "  return max(n + floor(t.x * 3.0 + 0.5), 0.0);\n" \
    "}\n" \
    /* The same in front of the pixel (right / down): the first pair is the next two pixels. This walk could tell one
       pixel more than the walk back when both run out of steps; it is cut to the same length, so a picture and its
       mirror image get the same result. */ \
    "float SmaaSearchForward(float2 uv, float2 axis) {\n" \
    "  float2 across = float2(axis.y, axis.x);\n" \
    "  float2 stepUv = axis * c0.xy;\n" \
    "  float2 p = uv + 1.25 * stepUv - 0.125 * across * c0.xy;\n" \
    "  float crossing = 0.0;\n" \
    "  float along = 1.0;\n" \
    "  float n = 0.0;\n" \
    "  for (int i = 0; i < SMAA_STEPS; i++) {\n" \
    "    float4 e = tex2Dlod(sEdges, float4(p, 0.0, 0.0));\n" \
    "    crossing = dot(e.xy, axis);\n" \
    "    along = dot(e.xy, across);\n" \
    "    if (along < 0.8281 || crossing > 0.0156) break;\n" \
    "    p = p + 2.0 * stepUv;\n" \
    "    n = n + 2.0;\n" \
    "  }\n" \
    "  float4 t = tex2Dlod(sSearch, float4((float2(crossing, along) * 32.0 + 0.5) / 64.0, 0.0, 0.0));\n" \
    "  return min(n + floor(t.y * 3.0 + 0.5), 2.0 * SMAA_STEPS - 1.0);\n" \
    "}\n" \
    /* The crossing edges at one end of the run: both sides in one fetch, 0.75 own side + 0.25 other side, which
       gives the end codes of smaa_lut.h (0 none, 1 other, 2 own, 3 both). */ \
    "float SmaaEndCode(float2 endUv, float2 sideUv, float2 axis) {\n" \
    "  float4 e = tex2Dlod(sEdges, float4(endUv - 0.25 * sideUv, 0.0, 0.0));\n" \
    "  return floor(dot(e.xy, axis) * 3.0 + 0.5);\n" \
    "}\n" \
    "float SmaaCrossingAt(float2 uv, float2 axis) {\n" \
    "  float4 e = tex2Dlod(sEdges, float4(uv, 0.0, 0.0));\n" \
    "  return dot(e.xy, axis);\n" \
    "}\n" \
    /* x = areaOther, y = areaOwn for the run through the low boundary of the pixel at uv (top for axis 1,0). */ \
    "float2 SmaaRunArea(float2 uv, float2 axis) {\n" \
    "  float2 stepUv = axis * c0.xy;\n" \
    "  float2 sideUv = float2(axis.y, axis.x) * c0.xy;\n" \
    "  float back = SmaaSearchBack(uv, axis);\n" \
    "  float ahead = SmaaSearchForward(uv, axis);\n" \
    "  float2 endA = uv - back * stepUv;\n" \
    "  float2 endB = uv + (ahead + 1.0) * stepUv;\n" \
    "  float codeA = SmaaEndCode(endA, sideUv, axis);\n" \
    "  float codeB = SmaaEndCode(endB, sideUv, axis);\n" \
    "  float2 cell = float2(codeA, codeB) * 64.0 + float2(min(back, 63.0), min(ahead, 63.0)) + 0.5;\n" \
    "  float4 area = tex2Dlod(sArea, float4(cell / 256.0, 0.0, 0.0));\n" \
    "  float2 result = float2(area.x, area.y);\n" \
    /* A step of a staircase is one pixel high. A crossing edge that goes on for a second pixel is a real corner of an
       object: what it would cut off is given back (all of it for c1.x = 1). Each half of the run is shaped by its own
       end, so the pixel asks the nearer end; exactly in the middle it asks both, and of those only an end that has
       a crossing edge on that side at all (codes 1 and 3: other side, 2 and 3: own side). */ \
    "  SMAA_BRANCH if (c1.x > 0.0) {\n" \
    "    float closeA = step(back, ahead);\n" \
    "    float closeB = step(ahead, back);\n" \
    "    float otherA = closeA * (codeA - 2.0 * floor(codeA * 0.5));\n" \
    "    float otherB = closeB * (codeB - 2.0 * floor(codeB * 0.5));\n" \
    "    float ownA = closeA * step(2.0, codeA);\n" \
    "    float ownB = closeB * step(2.0, codeB);\n" \
    "    float longOther = otherA * SmaaCrossingAt(endA - 2.0 * sideUv, axis) + otherB * SmaaCrossingAt(endB - 2.0 * sideUv, axis);\n" \
    "    float longOwn = ownA * SmaaCrossingAt(endA + sideUv, axis) + ownB * SmaaCrossingAt(endB + sideUv, axis);\n" \
    "    result.x = result.x * saturate(1.0 - c1.x * longOther / max(otherA + otherB, 1.0));\n" \
    "    result.y = result.y * saturate(1.0 - c1.x * longOwn / max(ownA + ownB, 1.0));\n" \
    "  }\n" \
    "  return result;\n" \
    "}\n" \
    "float4 SmaaWeights(float2 uv) {\n" \
    "  float4 e = tex2Dlod(sEdges, float4(uv, 0.0, 0.0));\n" \
    "  float4 w = float4(0.0, 0.0, 0.0, 0.0);\n" \
    "  SMAA_BRANCH if (e.y > 0.5) {\n" \
    "    float2 a = SmaaRunArea(uv, float2(1.0, 0.0));\n" \
    "    w.x = a.x;\n" \
    "    w.y = a.y;\n" \
    "  }\n" \
    "  SMAA_BRANCH if (e.x > 0.5) {\n" \
    "    float2 b = SmaaRunArea(uv, float2(0.0, 1.0));\n" \
    "    w.z = b.x;\n" \
    "    w.w = b.y;\n" \
    "  }\n" \
    "  return w;\n" \
    "}\n"

#define UFX_SMAA_WEIGHT_MAIN "float4 main(float2 uv : TEXCOORD0) : COLOR { return SmaaWeights(uv); }\n"

// ---- 3. Blend: every pixel takes its share of the neighbour across each edge --------------------------------------
// The picture is read with the linear filter: a fetch moved by `weight` of a pixel towards a neighbour is exactly
// lerp(pixel, neighbour, weight).
#define UFX_SMAA_BLEND_DECL \
    "sampler sColor : register(s0);\n"   /* the picture, linear filter */ \
    "sampler sWeights : register(s1);\n" /* point filter */ \
    "float4 c0 : register(c0);\n"        /* xy = texel size, z = strength 0..1 */

#define UFX_SMAA_BLEND_BODY \
    "float4 SmaaBlend(float2 uv) {\n" \
    "  float4 own = tex2Dlod(sWeights, float4(uv, 0.0, 0.0));\n" \
    "  float wTop = own.y;\n"  /* own top edge: this pixel is on the own side */ \
    "  float wLeft = own.w;\n" \
    "  float wBottom = tex2Dlod(sWeights, float4(uv + float2(0.0, c0.y), 0.0, 0.0)).x;\n"  /* the top edge of the pixel below: this one is on its other side */ \
    "  float wRight = tex2Dlod(sWeights, float4(uv + float2(c0.x, 0.0), 0.0, 0.0)).z;\n" \
    "  float4 c = tex2Dlod(sColor, float4(uv, 0.0, 0.0));\n" \
    "  if (wTop + wLeft + wBottom + wRight > 0.0) {\n" \
    /* only along the axis with the stronger weight: mixing both blurs corners */ \
    "    float hz = step(max(wTop, wBottom), max(wLeft, wRight) - 0.00001);\n" \
    "    float2 dirUv = float2(c0.x * hz, c0.y * (1.0 - hz));\n" \
    "    float wNeg = lerp(wTop, wLeft, hz);\n" \
    "    float wPos = lerp(wBottom, wRight, hz);\n" \
    "    float4 cNeg = tex2Dlod(sColor, float4(uv - wNeg * c0.z * dirUv, 0.0, 0.0));\n" \
    "    float4 cPos = tex2Dlod(sColor, float4(uv + wPos * c0.z * dirUv, 0.0, 0.0));\n" \
    "    c = (cNeg * wNeg + cPos * wPos) / (wNeg + wPos);\n" \
    "  }\n" \
    "  return float4(c.x, c.y, c.z, 1.0);\n" \
    "}\n"

#define UFX_SMAA_BLEND_MAIN "float4 main(float2 uv : TEXCOORD0) : COLOR { return SmaaBlend(uv); }\n"
