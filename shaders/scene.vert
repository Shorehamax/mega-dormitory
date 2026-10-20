#version 450
layout(location=0) in vec3 inPosition;
layout(location=1) in vec4 inColor;
layout(location=2) in vec2 inUV;
layout(location=3) in vec3 inNormal;
layout(location=4) in vec4 inMaterial;
layout(push_constant) uniform Push { mat4 mvp; float ui; vec3 eye; } pc;
layout(location=0) out vec4 color;
layout(location=1) out vec2 uv;
layout(location=2) out vec3 normal;
layout(location=3) out float depth;
layout(location=4) out vec3 worldPosition;
layout(location=5) out vec4 material;
struct Instance { vec4 position; vec4 scale; vec4 tint; };
layout(std430,binding=6) readonly buffer Instances { Instance instances[]; };
void main(){
 if(pc.ui<-.5){
  Instance i=instances[gl_InstanceIndex];
  vec3 p=inPosition*i.scale.xyz+i.position.xyz;
  gl_Position=pc.mvp*vec4(p,1);color=inColor*i.tint;uv=inUV;
  normal=normalize(inNormal/i.scale.xyz);depth=gl_Position.w;worldPosition=p;material=inMaterial;
  return;
 }
gl_Position=pc.mvp*vec4(inPosition,1);color=inColor;uv=inUV;normal=inNormal;depth=gl_Position.w;worldPosition=inPosition;material=inMaterial;}
