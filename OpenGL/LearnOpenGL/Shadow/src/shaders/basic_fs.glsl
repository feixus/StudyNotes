#version 450 core

out vec4 fragColor;

in VS_OUT
{
   vec3 FragPos;
   vec3 Normal;
   vec2 TexCoords;
   vec4 FragPosLightSpace;
} fs_in;

uniform sampler2D diffuseTexture;
uniform sampler2D shadowMap;

uniform vec3 lightPos;
uniform vec3 viewPos;

float ShadowCalculation(vec4 fragPosLightSpace, vec3 lightDir, vec3 normal)
{
   vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;  // [-w, +w] -> [-1, 1]
   projCoords = projCoords * 0.5 + 0.5;

   float currentDepth = projCoords.z;

   //shadow acne 
   float shadow = 0.0;
   vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
   float bias = max(0.05 * dot(lightDir, normal), 0.005);
   for (int x = -1; x <= 1; x++)
   {
      for (int y = -1; y <= 1; y++)
      {
         float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
         shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
      }
   }
   shadow /= 9.0;

   //超出距离1.0范围外的点不应该参与阴影计算
   //a light-space projected fragment coordinate is further than the light's far plane when its z coordinate is larger than 1.0
   if (currentDepth > 1.0)
      shadow = 0.0;

   return shadow;
}

void main()
{
   vec3 color = texture(diffuseTexture, fs_in.TexCoords).rgb;
   vec3 normal = normalize(fs_in.Normal);
   vec3 lightColor = vec3(0.8);

   vec3 ambient = 0.3 * lightColor;

   vec3 lightDir = normalize(lightPos - fs_in.FragPos);
   float diff = max(dot(lightDir, normal), 0.0);
   vec3 diffuse = diff * lightColor;

   vec3 viewDir = normalize(viewPos - fs_in.FragPos);
   float spec = 0.0;
   vec3 halfVector = normalize(lightDir + viewDir);
   spec = pow(max(dot(normal, halfVector), 0.0), 64.0);
   vec3 specular = spec * lightColor;

   float shadow = ShadowCalculation(fs_in.FragPosLightSpace, lightDir, normal);
   vec3 lighting = (ambient + (1.0 - shadow) * (diffuse + specular)) * color;
   
   fragColor = vec4(lighting, 1.0);
}



