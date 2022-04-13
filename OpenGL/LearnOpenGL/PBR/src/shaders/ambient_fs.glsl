#version 450 core

out vec4 fragColor;

in vec3 localPos;

uniform samplerCube irradianceMap;

const float PI = 3.14159265359;

void main()
{
   vec3 ambient = texture(irradiance, N).rgb;
   
   
}