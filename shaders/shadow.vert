#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec4 inColor;
layout(location=2) in vec2 inUV;
layout(location=4) in vec4 inMaterial;
layout(binding=5) uniform Lighting { mat4 lightMvp; } lighting;
layout(location=0) out vec2 uv;
layout(location=1) out vec2 alphaMaterial;
void main() {
 gl_Position=lighting.lightMvp*vec4(inPosition,1);
 uv=inUV;alphaMaterial=vec2(inMaterial.w<-.5?1:inColor.a,inMaterial.x);
}
