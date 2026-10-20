#version 450
layout(binding=1) uniform sampler2D albedoAtlas;
layout(location=0) in vec2 uv;
layout(location=1) in vec2 alphaMaterial;
void main() {
 float alpha=alphaMaterial.x;
 if(alphaMaterial.y>=0){
   vec2 cell=vec2(mod(floor(alphaMaterial.y+.5),5),floor((alphaMaterial.y+.5)/5));
   vec2 st=(cell+clamp(fract(uv),vec2(.001),vec2(.999)))/5;
   alpha*=textureGrad(albedoAtlas,st,dFdx(uv)/5,dFdy(uv)/5).a;
 }
 if(alpha<.4)discard;
}
