// https://web.archive.org/web/20191027010220/http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
float3 sRGB_to_XYZ(float3 rgb)
{
    float3 xyz;
    xyz.x = dot(float3(0.4124, 0.3576, 0.1805), rgb);  // rougnly corresponds to red-green perception
    xyz.y = dot(float3(0.2126, 0.7152, 0.0722), rgb);  // represents luminance(brightness)
    xyz.z = dot(float3(0.0193, 0.1192, 0.9505), rgb);  // roughly corresponds to blue-yellow perception
    // device-independent: XYZ can used as an intermediate space for color space conversion
    return xyz;
}

float3 XYZ_to_sRGB(float3 xyz)
{
    float3 rgb;
    rgb.x = dot(float3(3.2406, -1.5372, -0.4986), xyz);
    rgb.y = dot(float3(-0.9689, 1.8758, 0.0415), xyz);
    rgb.z = dot(float3(0.0557, -0.2040, 1.0570), xyz);
    return rgb;
}

float3 XYZ_to_xyY(float3 xyz)
{
    //  https://web.archive.org/web/20191027010144/http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_xyY.html
    float inv = 1.0 / dot(xyz, float3(1, 1, 1));
    return float3(xyz.x * inv, xyz.y * inv, xyz.y);
}

float3 xyY_to_XYZ(float3 xyY)
{
    // https://web.archive.org/web/20191027010036/http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
    float3 xyz;
    xyz.x = xyY.x * xyY.z / xyY.y;
    xyz.y = xyY.z;
    xyz.z = (1 - xyY.x - xyY.y) * xyY.z / xyY.y;
    return xyz;
}

float3 sRGB_to_xyY(float3 rgb)
{
    return XYZ_to_xyY(sRGB_to_XYZ(rgb));
}

float3 xyY_to_sRGB(float3 xyY)
{
    return XYZ_to_sRGB(xyY_to_XYZ(xyY));
}

float3 RGB_to_YCoCg(float3 c)
{
    // Y = R/4 + G/2 + B/4
    // Co = R/2 - B/2
    // Cg = -R/4 + G/2 - B/4
    return float3(c.g * 0.5 + (c.r + c.b) * 0.25, (c.r - c.b) * 0.5, c.g * 0.5 - (c.r + c.b) * 0.25);
}

float3 YCoCg_to_RGB(float3 c)
{
    // R = Y + Co - Cg
    // G = Y + Cg
    // B = Y - Co - Cg
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}