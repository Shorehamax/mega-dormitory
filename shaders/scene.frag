#version 450
layout(binding=0) uniform sampler2D fontAtlas;
layout(binding=1) uniform sampler2D albedoAtlas;
layout(binding=2) uniform sampler2D normalAtlas;
layout(binding=3) uniform sampler2D ormAtlas;
layout(binding=4) uniform sampler2DShadow shadowMap;
layout(binding=5) uniform Lighting { mat4 lightMvp; } lighting;
layout(push_constant) uniform Push { mat4 mvp; float ui; vec3 eye; } pc;
layout(location=0) in vec4 color;
layout(location=1) in vec2 uv;
layout(location=2) in vec3 normal;
layout(location=3) in float depth;
layout(location=4) in vec3 worldPosition;
layout(location=5) in vec4 material;
layout(location=0) out vec4 outColor;
const float PI=3.14159265359;
vec3 lit(vec3 base,vec3 n,vec3 view,vec3 light,vec3 radiance,float rough,float metal){
 vec3 h=normalize(view+light);float nl=max(dot(n,light),0),nv=max(dot(n,view),.001),nh=max(dot(n,h),0),vh=max(dot(view,h),0);
 float a=rough*rough,a2=a*a,d=nh*nh*(a2-1)+1;
 float distribution=a2/max(PI*d*d,.0001),k=(rough+1)*(rough+1)/8;
 float geometry=nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);
 vec3 f0=mix(vec3(.04),base,metal),fresnel=f0+(1-f0)*pow(1-vh,5);
 vec3 diffuse=(1-fresnel)*(1-metal)*base/PI;
 return (diffuse+distribution*geometry*fresnel/max(4*nv*nl,.001))*radiance*nl;
}
float visibility(vec3 p, vec3 n) {
 vec4 projected=lighting.lightMvp*vec4(p+n*.035,1);
 vec3 q=projected.xyz/projected.w;q.xy=q.xy*.5+.5;
 if(q.z<=0 || q.z>=1 || any(lessThan(q.xy,vec2(0))) || any(greaterThan(q.xy,vec2(1)))) return 1;
 vec2 texel=1.0/vec2(textureSize(shadowMap,0));float sum=0;
 for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)
   sum+=texture(shadowMap,vec3(q.xy+vec2(x,y)*texel,q.z-.00025));
 float edge=min(min(q.x,q.y),min(1-q.x,1-q.y));
 return mix(1.0,sum/9.0,smoothstep(0,.06,edge));
}
void main(){
 if(pc.ui>.5){outColor=vec4(pow(color.rgb,vec3(2.2)),color.a*texture(fontAtlas,uv).r);return;}
 vec3 n=normalize(normal),base=pow(color.rgb,vec3(2.2));
 float rough=material.y,metal=material.z,ao=1,alpha=color.a;
 bool garment=material.w<-.5; if(garment)alpha=1;
 if(material.x>=0){
   vec2 cell=vec2(mod(floor(material.x+.5),5),floor((material.x+.5)/5));
   // Match the border guard to the mip footprint; never sample a neighbouring material.
   vec2 dx=dFdx(uv),dy=dFdy(uv);
   float footprint=max(length(dx),length(dy))*768;
   float mip=clamp(log2(max(footprint,1)),0,6);
   float guard=(exp2(mip)*.75+1)/768;
   vec2 st=(cell+clamp(fract(uv),vec2(guard),vec2(1-guard)))/5;
   vec4 texel=textureGrad(albedoAtlas,st,dFdx(uv)/5,dFdy(uv)/5);base=garment?mix(texel.rgb,pow(color.rgb,vec3(2.2)),color.a):texel.rgb*color.rgb;alpha*=texel.a;
   vec3 orm=textureGrad(ormAtlas,st,dFdx(uv)/5,dFdy(uv)/5).rgb;ao=orm.r;rough*=orm.g;metal*=orm.b;
   vec3 mapped=textureGrad(normalAtlas,st,dFdx(uv)/5,dFdy(uv)/5).xyz*2-1; mapped.xy*=garment?mix(1.,.12,color.a):material.w;
   vec3 dp1=dFdx(worldPosition),dp2=dFdy(worldPosition);vec2 duv1=dFdx(uv),duv2=dFdy(uv);
   float det=duv1.x*duv2.y-duv1.y*duv2.x;
   if(abs(det)>.000001){vec3 tangent=normalize((dp1*duv2.y-dp2*duv1.y)/det);tangent=normalize(tangent-n*dot(tangent,n));vec3 bitangent=normalize(cross(n,tangent))*sign(det);n=normalize(mat3(tangent,bitangent,n)*mapped);}
 }
 if(garment){rough=mix(rough,.92,color.a);metal=mix(metal,0.,color.a);}
 if(alpha<.4)discard;
 if(!gl_FrontFacing)n=-n;
 rough=clamp(rough,.08,1);metal=clamp(metal,0,1);
 vec3 view=normalize(pc.eye-worldPosition);
 vec3 c=base*vec3(.23,.27,.32)*ao;
 c+=lit(base,n,view,normalize(vec3(-.35,.85,.3)),vec3(1.0,1.09,1.13),rough,metal)*visibility(worldPosition,n);
 float floorY=floor((worldPosition.y+.02)/3)*3;
 vec2 grid=floor(worldPosition.xz/12)*12;
 for(int x=0;x<2;++x)for(int z=0;z<2;++z){
   vec3 bulb=vec3(grid.x+x*12,floorY+2.85,grid.y+z*12);
   vec3 delta=bulb-worldPosition;float distance2=max(dot(delta,delta),.1);
   c+=lit(base,n,view,normalize(delta),vec3(6.8,7.3,7.5)/(1+distance2*.22),rough,metal)*ao;
 }
 if(material.x < -1.5)c=base*2.5;
 // Exposure/filmic roll-off, followed by the swapchain's sRGB conversion.
 c*=1.4;c=clamp((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14),0,1);
 float fog=1-exp(-max(depth-18,0)*.012);c=mix(c,vec3(.06,.09,.11),min(fog,.82));
 outColor=vec4(c,1);
}
