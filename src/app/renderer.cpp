#include "renderer.hpp"
#include "fasttris/version.hpp"
#include "fasttris/tetromino.hpp"
#include <algorithm>
#include <cstdarg>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace fasttris::app {
namespace {
struct C { Uint8 r,g,b,a; };
struct Canvas { float w{960.0f}; float h{720.0f}; float scale{1.0f}; };

float g_offset_x = 0.0f;
float g_offset_y = 0.0f;
VisualPalette g_visual_palette = VisualPalette::Default;
bool g_palette_affects_pieces = true;
bool g_palette_transform_enabled = true;
struct TextureRuntime {
    VisualTexture mode{VisualTexture::Default};
    int cell_gap{2};
    int depth{35};
    int highlight{35};
    int shadow{30};
    int border{2};
    int softness{35};
    int reflection{40};
    int edge_light{40};
    int inner_darken{20};
    int transparency{10};
    int contrast{30};
    int pattern_scale{4};
    int dot_size{2};
    int dot_spacing{6};
    int angle{1};
    int spacing{5};
    int line_thickness{1};
    int grid_size{6};
};
TextureRuntime g_texture{};

struct ShaderRuntime {
    VisualShader mode{VisualShader::None};
    int strength{50};
    int scanlines{35};
    int scanline_spacing{4};
    int glow{30};
    int curvature{35};
    int vignette{25};
    int softness{15};
    int persistence{25};
    int flicker{10};
    int pixel_grid{20};
    int grid_size{8};
    int subpixel{25};
    int sharpness{60};
    int dot_size{2};
    int dot_spacing{6};
    int dot_brightness{35};
    int radius{50};
    int threshold{65};
    int trail_length{3};
    int noise{20};
    int horizontal_jitter{10};
    int distortion{15};
    int rgb_offset{2};
    int direction{0};
    int line_thickness{1};
    int bloom{25};
};
ShaderRuntime g_shader{};

constexpr int kWarpCols=32;
constexpr int kWarpRows=24;
constexpr int kWarpVertexCount=(kWarpCols+1)*(kWarpRows+1);
constexpr int kWarpIndexCount=kWarpCols*kWarpRows*6;
struct PostProcessRuntime {
    SDL_Renderer* renderer{};
    SDL_Texture* frame{};
    SDL_Texture* blur{};
    SDL_Texture* history_a{};
    SDL_Texture* history_b{};
    int width{};
    int height{};
    bool capturing{};
    bool target_unavailable{};
    bool history_valid{};
    bool blur_ready{};
    std::array<SDL_Vertex,kWarpVertexCount> warp_vertices{};
    std::array<int,kWarpIndexCount> warp_indices{};
    int warp_width{-1};
    int warp_height{-1};
    int warp_curve_key{-1};
    int warp_alpha_key{-1};
    bool warp_indices_ready{};
};
PostProcessRuntime g_post{};

Uint8 clampByte(int value){return static_cast<Uint8>(std::clamp(value,0,255));}
Uint8 lerpByte(Uint8 a,Uint8 b,int t,int scale){
    t=std::clamp(t,0,scale);
    return clampByte((int(a)*(scale-t)+int(b)*t+scale/2)/scale);
}
C gradientPalette(C low,C mid,C high,int luminance,Uint8 alpha_value){
    if(luminance<=127){
        const int t=luminance*255/127;
        return {lerpByte(low.r,mid.r,t,255),lerpByte(low.g,mid.g,t,255),lerpByte(low.b,mid.b,t,255),alpha_value};
    }
    const int t=(luminance-128)*255/127;
    return {lerpByte(mid.r,high.r,t,255),lerpByte(mid.g,high.g,t,255),lerpByte(mid.b,high.b,t,255),alpha_value};
}
C paletteColor(C c){
    if(!g_palette_transform_enabled||g_visual_palette==VisualPalette::Default)return c;
    const int y=(54*int(c.r)+183*int(c.g)+19*int(c.b)+128)>>8;
    switch(g_visual_palette){
        case VisualPalette::Hacker:
            return {clampByte(y*18/100),clampByte(y*108/100),clampByte(y*34/100),c.a};
        case VisualPalette::Amber:
            return {clampByte(y*112/100),clampByte(y*66/100),clampByte(y*12/100),c.a};
        case VisualPalette::BlackWhite:
            return {static_cast<Uint8>(y),static_cast<Uint8>(y),static_cast<Uint8>(y),c.a};
        case VisualPalette::MintBlue:
            return {clampByte(y*48/100),clampByte(y*100/100),clampByte(y*112/100),c.a};
        case VisualPalette::LofiWarm:
            return gradientPalette({29,24,23,255},{169,102,75,255},{232,211,169,255},y,c.a);
        case VisualPalette::LofiCool:
            return gradientPalette({21,27,38,255},{104,132,157,255},{205,216,198,255},y,c.a);
        case VisualPalette::PastelBlue:
            return gradientPalette({25,38,56,255},{126,180,217,255},{220,239,248,255},y,c.a);
        case VisualPalette::Halloween:
            return gradientPalette({24,8,33,255},{185,58,14,255},{255,184,61,255},y,c.a);
        case VisualPalette::Sunset:
            return gradientPalette({25,20,66,255},{226,88,102,255},{255,207,126,255},y,c.a);
        case VisualPalette::Sunrise:
            return gradientPalette({38,76,112,255},{151,193,218,255},{255,206,146,255},y,c.a);
        default:
            return c;
    }
}

Canvas beginCanvas(SDL_Renderer* r, bool center_design = true) {
    int output_w = 960;
    int output_h = 720;
    if (!SDL_GetRenderOutputSize(r, &output_w, &output_h) || output_w <= 0 || output_h <= 0) {
        output_w = 960;
        output_h = 720;
    }

    constexpr float design_w = 960.0f;
    constexpr float design_h = 720.0f;
    const float scale = std::max(0.1f, std::min(output_w / design_w, output_h / design_h));
    const float canvas_w = output_w / scale;
    const float canvas_h = output_h / scale;

    SDL_SetRenderScale(r, scale, scale);
    g_offset_x = center_design ? std::max(0.0f, (canvas_w - design_w) * 0.5f) : 0.0f;
    g_offset_y = center_design ? std::max(0.0f, (canvas_h - design_h) * 0.5f) : 0.0f;
    return {canvas_w, canvas_h, scale};
}

C color(Piece p){switch(p){case Piece::I:return{60,210,230,255};case Piece::J:return{70,100,230,255};case Piece::L:return{240,150,55,255};case Piece::O:return{235,210,65,255};case Piece::S:return{80,210,100,255};case Piece::T:return{175,80,220,255};case Piece::Z:return{225,70,80,255};case Piece::Garbage:return{100,110,120,255};default:return{0,0,0,0};}}
void set(SDL_Renderer*r,C c){c=paletteColor(c);SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);}
void fill(SDL_Renderer*r,float x,float y,float w,float h,C c){set(r,c);SDL_FRect q{x+g_offset_x,y+g_offset_y,w,h};SDL_RenderFillRect(r,&q);}
void outline(SDL_Renderer*r,float x,float y,float w,float h,C c){set(r,c);SDL_FRect q{x+g_offset_x,y+g_offset_y,w,h};SDL_RenderRect(r,&q);}
void outlinePiece(SDL_Renderer*r,float x,float y,float w,float h,C c){
    const bool previous=g_palette_transform_enabled;
    g_palette_transform_enabled=g_palette_affects_pieces;
    outline(r,x,y,w,h,c);
    g_palette_transform_enabled=previous;
}
void txt(SDL_Renderer*r,float x,float y,const std::string&s,C c={220,225,232,255},bool big=false){
    set(r,c);
    const float px=x+g_offset_x,py=y+g_offset_y;
    if(big){
        float sx=1.0f,sy=1.0f;
        SDL_GetRenderScale(r,&sx,&sy);
        SDL_SetRenderScale(r,sx*2.0f,sy*2.0f);
        SDL_RenderDebugText(r,px/2.0f,py/2.0f,s.c_str());
        SDL_SetRenderScale(r,sx,sy);
    }else SDL_RenderDebugText(r,px,py,s.c_str());
}
void txtf(SDL_Renderer*r,float x,float y,C c,const char*fmt,...){char b[256];va_list ap;va_start(ap,fmt);std::vsnprintf(b,sizeof(b),fmt,ap);va_end(ap);txt(r,x,y,b,c);}
const char* clearName(ClearKind k){switch(k){case ClearKind::Single:return"SINGLE";case ClearKind::Double:return"DOUBLE";case ClearKind::Triple:return"TRIPLE";case ClearKind::Quad:return"QUAD";case ClearKind::MiniNoLine:return"T-SPIN MINI";case ClearKind::MiniSingle:return"T-SPIN MINI SINGLE";case ClearKind::MiniDouble:return"T-SPIN MINI DOUBLE";case ClearKind::TSpinNoLine:return"T-SPIN";case ClearKind::TSpinSingle:return"T-SPIN SINGLE";case ClearKind::TSpinDouble:return"T-SPIN DOUBLE";case ClearKind::TSpinTriple:return"T-SPIN TRIPLE";default:return"";}}

C shade(C c,int amount){
    amount=std::clamp(amount,-100,100);
    auto one=[&](Uint8 v){
        if(amount>=0)return clampByte(int(v)+(255-int(v))*amount/100);
        return clampByte(int(v)*(100+amount)/100);
    };
    return {one(c.r),one(c.g),one(c.b),c.a};
}
C alpha(C c,int a){c.a=static_cast<Uint8>(std::clamp(a,0,255));return c;}

template<std::size_t N>
void fillBatch(SDL_Renderer*r,std::array<SDL_FRect,N>&rects,int count,C c){
    if(count<=0)return;
    c=paletteColor(c);SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);
    for(int i=0;i<count;++i){rects[static_cast<std::size_t>(i)].x+=g_offset_x;rects[static_cast<std::size_t>(i)].y+=g_offset_y;}
    SDL_RenderFillRects(r,rects.data(),count);
}

void drawNestedBorder(SDL_Renderer*r,float x,float y,float w,float h,int thickness,C c){
    thickness=std::clamp(thickness,1,6);
    for(int i=0;i<thickness;++i){
        const float inset=float(i);
        if(w-inset*2<=1||h-inset*2<=1)break;
        outline(r,x+inset,y+inset,w-inset*2,h-inset*2,c);
    }
}

void drawTexturedCell(SDL_Renderer*r,float x,float y,float size,C base){
    const bool previous_palette_transform=g_palette_transform_enabled;
    g_palette_transform_enabled=g_palette_affects_pieces;
    const int max_gap=std::max(0,std::min(6,int(size)-4));
    const float gap=float(std::clamp(g_texture.cell_gap,0,max_gap));
    const float ix=x+gap*0.5f,iy=y+gap*0.5f;
    const float w=std::max(2.0f,size-gap),h=w;
    const int border=std::clamp(g_texture.border,1,std::max(1,std::min(6,int(w/4))));
    const C hi=shade(base,std::clamp(g_texture.highlight,0,100));
    const C sh=shade(base,-std::clamp(g_texture.shadow,0,100));
    const C edge=shade(base,std::clamp(g_texture.edge_light,0,100));
    const C inner=shade(base,-std::clamp(g_texture.inner_darken,0,100));

    switch(g_texture.mode){
        case VisualTexture::Default:{
            fill(r,ix,iy,w,h,base);
            const float pad=std::max(1.0f,std::min(3.0f,w*0.20f));
            const float line=std::max(1.0f,std::min(4.0f,h-pad*2));
            const C top{clampByte(int(base.r)+35),clampByte(int(base.g)+35),clampByte(int(base.b)+35),base.a};
            fill(r,ix+pad,iy+pad,std::max(1.0f,w-pad*2),line,top);
            break;
        }
        case VisualTexture::Flat:
            fill(r,ix,iy,w,h,base);
            break;
        case VisualTexture::Beveled:{
            fill(r,ix,iy,w,h,shade(base,-4));
            const float d=std::clamp(1.0f+w*(g_texture.depth/100.0f)*0.18f,1.0f,std::max(1.0f,w*0.25f));
            fill(r,ix,iy,w,d,hi);fill(r,ix,iy,d,h,hi);
            fill(r,ix,iy+h-d,w,d,sh);fill(r,ix+w-d,iy,d,h,sh);
            drawNestedBorder(r,ix,iy,w,h,border,shade(base,-12));
            const float inset=std::min(std::max(1.0f,d),std::max(1.0f,std::min(w,h)*0.28f));
            if(w>inset*2.0f+1.0f&&h>inset*2.0f+1.0f){
                fill(r,ix+inset,iy+inset,w-inset*2.0f,h-inset*2.0f,shade(base,6+g_texture.softness/6));
                const int layers=1+g_texture.softness/40;
                for(int n=0;n<layers;++n){
                    const float s=inset+float(n);
                    if(w-s*2.0f<=1.0f||h-s*2.0f<=1.0f)break;
                    const int a=std::max(20,120-n*28);
                    fill(r,ix+s,iy+s,w-s*2.0f,1.0f,alpha(hi,a));
                    fill(r,ix+s,iy+s,1.0f,h-s*2.0f,alpha(hi,a));
                    fill(r,ix+s,iy+h-s-1.0f,w-s*2.0f,1.0f,alpha(sh,a));
                    fill(r,ix+w-s-1.0f,iy+s,1.0f,h-s*2.0f,alpha(sh,a));
                }
            }
            break;
        }
        case VisualTexture::SoftBevel:{
            fill(r,ix,iy,w,h,shade(base,4));
            const int layers=2+g_texture.softness/25;
            const float maxd=std::clamp(1.0f+w*(g_texture.depth/100.0f)*0.13f,1.0f,std::max(1.0f,w*0.20f));
            for(int n=0;n<layers;++n){
                const float inset=float(n)*0.8f;
                const float d=std::max(1.0f,maxd-inset*0.6f);
                if(w-inset*2.0f<=1.0f||h-inset*2.0f<=1.0f)break;
                const int fade=std::max(10,125-n*18);
                fill(r,ix+inset,iy+inset,w-inset*2.0f,1.0f,alpha(hi,fade));
                fill(r,ix+inset,iy+inset,1.0f,h-inset*2.0f,alpha(hi,fade));
                fill(r,ix+inset,iy+h-inset-d,w-inset*2.0f,1.0f,alpha(sh,fade));
                fill(r,ix+w-inset-d,iy+inset,1.0f,h-inset*2.0f,alpha(sh,fade));
            }
            const float inner_inset=std::min(std::max(1.0f,maxd+1.0f),std::min(w,h)*0.30f);
            if(w>inner_inset*2.0f+1.0f&&h>inner_inset*2.0f+1.0f){
                fill(r,ix+inner_inset,iy+inner_inset,w-inner_inset*2.0f,h-inner_inset*2.0f,shade(base,10+g_texture.highlight/8));
                fill(r,ix+inner_inset*1.15f,iy+inner_inset*1.10f,w*0.52f,std::max(1.0f,h*0.12f),alpha(shade(base,75),70+g_texture.highlight));
            }
            break;
        }
        case VisualTexture::Glass:{
            const int a=255*(100-std::clamp(g_texture.transparency,0,70))/100;
            fill(r,ix,iy,w,h,alpha(shade(base,8),a));
            fill(r,ix+w*0.08f,iy+h*0.54f,w*0.84f,h*0.30f,alpha(shade(base,-28-g_texture.inner_darken/3),std::max(55,a*3/4)));
            const float band=std::max(1.0f,h*(0.10f+0.16f*g_texture.reflection/100.0f));
            fill(r,ix+w*0.08f,iy+h*0.11f,w*0.84f,band,alpha(shade(base,78),55+g_texture.reflection*2));
            fill(r,ix+w*0.18f,iy+h*0.26f,w*0.40f,std::max(1.0f,h*0.08f),alpha(shade(base,92),35+g_texture.reflection));
            fill(r,ix+w*0.55f,iy+h*0.18f,w*0.18f,h*0.45f,alpha(shade(base,88),18+g_texture.reflection));
            drawNestedBorder(r,ix,iy,w,h,1,alpha(edge,110+g_texture.edge_light));
            if(w>8.0f&&h>8.0f)drawNestedBorder(r,ix+1.0f,iy+1.0f,w-2.0f,h-2.0f,1,alpha(shade(base,70),50+g_texture.edge_light));
            break;
        }
        case VisualTexture::Neon:
            fill(r,ix,iy,w,h,shade(base,-45-g_texture.inner_darken/3));
            drawNestedBorder(r,ix,iy,w,h,border,edge);
            if(w>8)drawNestedBorder(r,ix+border+1,iy+border+1,w-2*(border+1),h-2*(border+1),1,alpha(shade(base,65),90+g_texture.edge_light));
            break;
        case VisualTexture::Metallic:{
            fill(r,ix,iy,w,h,shade(base,-14));
            std::array<SDL_FRect,24> rects{};int count=0;
            const int spacing=std::max(3,g_texture.spacing);
            const int thick=std::clamp(g_texture.line_thickness,1,4);
            for(float yy=iy+1.0f;yy<iy+h-1.0f&&count<int(rects.size());yy+=spacing)rects[count++]={ix+1.0f,yy,w-2.0f,float(thick)};
            fillBatch(r,rects,count,alpha(shade(base,28+g_texture.contrast/3),70+g_texture.highlight));
            fill(r,ix+w*0.10f,iy+h*0.18f,w*0.72f,std::max(1.0f,h*0.12f),alpha(shade(base,72),70+g_texture.highlight));
            fill(r,ix+w*0.22f,iy+h*0.46f,w*0.58f,std::max(1.0f,h*0.10f),alpha(shade(base,58),55+g_texture.contrast));
            fill(r,ix,iy,1.0f,h,shade(base,32));
            fill(r,ix+w-1.0f,iy,1.0f,h,shade(base,-35));
            drawNestedBorder(r,ix,iy,w,h,1,alpha(shade(base,-22),90));
            break;
        }
        case VisualTexture::Pixel:{
            fill(r,ix,iy,w,h,shade(base,-8));
            std::array<SDL_FRect,32> bright{};std::array<SDL_FRect,32> dark{};
            int bright_count=0,dark_count=0;
            const float ps=float(std::max(2,g_texture.pattern_scale));
            int row=0;
            for(float yy=iy+1.0f;yy<iy+h-1.0f&&(bright_count<int(bright.size())||dark_count<int(dark.size()));yy+=ps,++row){
                int col=0;
                for(float xx=ix+1.0f;xx<ix+w-1.0f&&(bright_count<int(bright.size())||dark_count<int(dark.size()));xx+=ps,++col){
                    const float pw=std::min(ps-1.0f,ix+w-xx-1.0f),ph=std::min(ps-1.0f,iy+h-yy-1.0f);
                    if(pw<=0.0f||ph<=0.0f)continue;
                    const int pattern=(row*3+col*5)&3;
                    if(pattern==0&&bright_count<int(bright.size()))bright[bright_count++]={xx,yy,pw,ph};
                    else if(pattern==1&&dark_count<int(dark.size()))dark[dark_count++]={xx,yy,pw,ph};
                }
            }
            fillBatch(r,bright,bright_count,alpha(shade(base,24+g_texture.contrast/5),110+g_texture.contrast));
            fillBatch(r,dark,dark_count,alpha(shade(base,-18-g_texture.contrast/3),95+g_texture.contrast));
            drawNestedBorder(r,ix,iy,w,h,1,alpha(shade(base,-22),85));
            break;
        }
        case VisualTexture::Dots:{
            fill(r,ix,iy,w,h,base);
            std::array<SDL_FRect,32> rects{};int count=0;
            const float spacing=float(std::max(g_texture.dot_spacing,g_texture.dot_size+1));
            const float dot=float(std::min(g_texture.dot_size,int(std::max(1.0f,w/4))));
            for(float yy=iy+spacing*0.5f;yy+dot<=iy+h&&count<int(rects.size());yy+=spacing)
                for(float xx=ix+spacing*0.5f;xx+dot<=ix+w&&count<int(rects.size());xx+=spacing)rects[count++]={xx,yy,dot,dot};
            fillBatch(r,rects,count,alpha(shade(base,25+g_texture.contrast/2),130+g_texture.contrast));
            break;
        }
        case VisualTexture::Stripes:{
            fill(r,ix,iy,w,h,shade(base,-6));
            std::array<SDL_FRect,32> light{};std::array<SDL_FRect,32> dark{};int lc=0,dc=0;
            const int spacing=std::max(3,g_texture.spacing),thick=std::clamp(g_texture.line_thickness,1,4);
            if(g_texture.angle==0){
                for(float yy=iy;yy<iy+h&&lc<int(light.size());yy+=spacing){light[lc++]={ix,yy,w,float(thick)};if(dc<int(dark.size()))dark[dc++]={ix,yy+thick,w,1.0f};}
            }else if(g_texture.angle==2){
                for(float xx=ix;xx<ix+w&&lc<int(light.size());xx+=spacing){light[lc++]={xx,iy,float(thick),h};if(dc<int(dark.size()))dark[dc++]={xx+thick,iy,1.0f,h};}
            }else{
                const int direction=g_texture.angle==1?1:-1;
                const int tile=std::max(2,spacing/2);
                for(int row=0;row<int(h)&&lc<int(light.size());row+=tile){
                    int phase=(direction*row)%spacing;if(phase<0)phase+=spacing;
                    for(int col=-phase;col<int(w)&&lc<int(light.size());col+=spacing){
                        const int x0=std::max(0,col),x1=std::min(int(w),col+std::max(1,thick+1));
                        if(x1>x0)light[lc++]={ix+float(x0),iy+float(row),float(x1-x0),float(std::min(tile,int(h)-row))};
                    }
                }
            }
            fillBatch(r,light,lc,alpha(shade(base,22+g_texture.contrast/4),85+g_texture.contrast));
            fillBatch(r,dark,dc,alpha(shade(base,-24-g_texture.contrast/3),65+g_texture.contrast/2));
            break;
        }
        case VisualTexture::Grid:{
            fill(r,ix,iy,w,h,shade(base,4));
            std::array<SDL_FRect,32> gutters{};std::array<SDL_FRect,32> highlights{};int gc=0,hc=0;
            const int step=std::max(3,g_texture.grid_size),thick=std::clamp(g_texture.line_thickness,1,4);
            for(float xx=ix+step;xx<ix+w&&gc<int(gutters.size());xx+=step){
                gutters[gc++]={xx,iy,float(thick),h};
                if(hc<int(highlights.size())&&xx+thick<ix+w)highlights[hc++]={xx+thick,iy,1.0f,h};
            }
            for(float yy=iy+step;yy<iy+h&&gc<int(gutters.size());yy+=step){
                gutters[gc++]={ix,yy,w,float(thick)};
                if(hc<int(highlights.size())&&yy+thick<iy+h)highlights[hc++]={ix,yy+thick,w,1.0f};
            }
            fillBatch(r,gutters,gc,alpha(shade(base,-32-g_texture.contrast/4),95+g_texture.contrast));
            fillBatch(r,highlights,hc,alpha(shade(base,35+g_texture.contrast/5),45+g_texture.contrast/2));
            drawNestedBorder(r,ix,iy,w,h,1,alpha(shade(base,-20),90));
            break;
        }
        case VisualTexture::Wireframe:{
            const int interior_alpha=std::clamp(125-g_texture.transparency,35,135);
            fill(r,ix,iy,w,h,alpha(shade(base,-45-g_texture.inner_darken/2),interior_alpha));
            drawNestedBorder(r,ix,iy,w,h,border,edge);
            if(w>8.0f&&h>8.0f){
                const float inset=float(border)+1.0f;
                if(w>inset*2.0f+1.0f&&h>inset*2.0f+1.0f)drawNestedBorder(r,ix+inset,iy+inset,w-inset*2.0f,h-inset*2.0f,1,alpha(shade(base,55),75+g_texture.edge_light));
                if(g_texture.transparency<45){
                    const float brace=std::max(1.0f,float(border-1));
                    fill(r,ix+w*0.5f-brace*0.5f,iy+inset,brace,h-inset*2.0f,alpha(edge,70+g_texture.edge_light));
                    fill(r,ix+inset,iy+h*0.5f-brace*0.5f,w-inset*2.0f,brace,alpha(edge,70+g_texture.edge_light));
                }
            }
            break;
        }
        case VisualTexture::Outline:
            fill(r,ix,iy,w,h,inner);
            drawNestedBorder(r,ix,iy,w,h,border,base);
            break;
        case VisualTexture::Recessed:{
            fill(r,ix,iy,w,h,base);
            const float d=std::clamp(1.0f+w*(g_texture.depth/100.0f)*0.14f,1.0f,std::max(1.0f,w*0.20f));
            fill(r,ix,iy,w,d,sh);fill(r,ix,iy,d,h,sh);fill(r,ix,iy+h-d,w,d,hi);fill(r,ix+w-d,iy,d,h,hi);
            fill(r,ix+d,iy+d,w-2*d,h-2*d,shade(base,-8));
            break;
        }
        case VisualTexture::Arcade:{
            fill(r,ix,iy,w,h,base);
            const float d=std::max(2.0f,std::min(5.0f,1.0f+w*g_texture.depth/650.0f));
            fill(r,ix,iy,w,d,shade(base,30+g_texture.highlight/2));
            fill(r,ix,iy,d,h,shade(base,20+g_texture.edge_light/2));
            fill(r,ix,iy+h-d,w,d,shade(base,-30));fill(r,ix+w-d,iy,d,h,shade(base,-30));
            drawNestedBorder(r,ix,iy,w,h,border,shade(base,-18));
            fill(r,ix+w*0.18f,iy+h*0.18f,w*0.45f,std::max(1.0f,h*0.10f),alpha(shade(base,70),120+g_texture.highlight));
            break;
        }
        case VisualTexture::RetroLCD:{
            fill(r,ix,iy,w,h,shade(base,-20-g_texture.inner_darken/3));
            std::array<SDL_FRect,32> rects{};int count=0;
            const int step=std::max(3,g_texture.grid_size);
            const float dot=std::max(1.0f,std::min(float(g_texture.dot_spacing/3),float(step-1)));
            for(float yy=iy+2;yy<iy+h-1&&count<int(rects.size());yy+=step)
                for(float xx=ix+2;xx<ix+w-1&&count<int(rects.size());xx+=step)rects[count++]={xx,yy,dot,dot};
            fillBatch(r,rects,count,alpha(shade(base,35+g_texture.contrast/3),115+g_texture.contrast));
            drawNestedBorder(r,ix,iy,w,h,1,shade(base,-35));
            break;
        }
        default:
            fill(r,ix,iy,w,h,base);
            break;
    }
    g_palette_transform_enabled=previous_palette_transform;
}

void drawMini(SDL_Renderer*r,Piece p,float ox,float oy,float s){if(p==Piece::None)return;auto bs=blocks(p,Rotation::Spawn);int minx=4,maxx=0,miny=4,maxy=0;for(auto b:bs){minx=std::min(minx,b.x);maxx=std::max(maxx,b.x);miny=std::min(miny,b.y);maxy=std::max(maxy,b.y);}float cx=ox-(minx+maxx+1)*s/2.0f,cy=oy-(miny+maxy+1)*s/2.0f;for(auto b:bs)drawTexturedCell(r,cx+b.x*s,cy+b.y*s,s,color(p));}
void actionLabel(SDL_Renderer*r,float x,float y,const ReplayEvent&e){std::string s=std::string(e.down?"+":"-")+std::string(actionName(e.action));txt(r,x,y,s,{150,160,175,255});}
}

void setVisualPalette(VisualPalette palette,bool affects_pieces){
    const int value=std::clamp(static_cast<int>(palette),0,kVisualPaletteCount-1);
    g_visual_palette=static_cast<VisualPalette>(value);
    g_palette_affects_pieces=affects_pieces;
}

void setVisualTexture(const AppConfig& cfg){
    g_texture.mode=cfg.texture;
    g_texture.cell_gap=cfg.texture_cell_gap;
    g_texture.depth=cfg.texture_depth;
    g_texture.highlight=cfg.texture_highlight;
    g_texture.shadow=cfg.texture_shadow;
    g_texture.border=cfg.texture_border;
    g_texture.softness=cfg.texture_softness;
    g_texture.reflection=cfg.texture_reflection;
    g_texture.edge_light=cfg.texture_edge_light;
    g_texture.inner_darken=cfg.texture_inner_darken;
    g_texture.transparency=cfg.texture_transparency;
    g_texture.contrast=cfg.texture_contrast;
    g_texture.pattern_scale=cfg.texture_pattern_scale;
    g_texture.dot_size=cfg.texture_dot_size;
    g_texture.dot_spacing=cfg.texture_dot_spacing;
    g_texture.angle=cfg.texture_angle;
    g_texture.spacing=cfg.texture_spacing;
    g_texture.line_thickness=cfg.texture_line_thickness;
    g_texture.grid_size=cfg.texture_grid_size;
}

void setVisualShader(const AppConfig& cfg){
    const VisualShader previous_mode=g_shader.mode;
    g_shader.mode=cfg.shader;
    g_shader.strength=cfg.shader_strength;
    g_shader.scanlines=cfg.shader_scanlines;
    g_shader.scanline_spacing=cfg.shader_scanline_spacing;
    g_shader.glow=cfg.shader_glow;
    g_shader.curvature=cfg.crt_curvature;
    g_shader.vignette=cfg.shader_vignette;
    g_shader.softness=cfg.shader_softness;
    g_shader.persistence=cfg.shader_persistence;
    g_shader.flicker=cfg.shader_flicker;
    g_shader.pixel_grid=cfg.shader_pixel_grid;
    g_shader.grid_size=cfg.shader_grid_size;
    g_shader.subpixel=cfg.shader_subpixel;
    g_shader.sharpness=cfg.shader_sharpness;
    g_shader.dot_size=cfg.shader_dot_size;
    g_shader.dot_spacing=cfg.shader_dot_spacing;
    g_shader.dot_brightness=cfg.shader_dot_brightness;
    g_shader.radius=cfg.shader_radius;
    g_shader.threshold=cfg.shader_threshold;
    g_shader.trail_length=cfg.shader_trail_length;
    g_shader.noise=cfg.shader_noise;
    g_shader.horizontal_jitter=cfg.shader_horizontal_jitter;
    g_shader.distortion=cfg.shader_distortion;
    g_shader.rgb_offset=cfg.shader_rgb_offset;
    g_shader.direction=cfg.shader_direction;
    g_shader.line_thickness=cfg.shader_line_thickness;
    g_shader.bloom=cfg.shader_bloom;
    if(previous_mode!=g_shader.mode)g_post.history_valid=false;
}

namespace {
void setRaw(SDL_Renderer*r,C c){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);}
void fillAbs(SDL_Renderer*r,float x,float y,float w,float h,C c){setRaw(r,c);SDL_FRect q{x,y,w,h};SDL_RenderFillRect(r,&q);}
void outlineAbs(SDL_Renderer*r,float x,float y,float w,float h,C c){setRaw(r,c);SDL_FRect q{x,y,w,h};SDL_RenderRect(r,&q);}
int effectAlpha(int local_percent,int max_alpha=255){
    const int local=std::clamp(local_percent,0,100);
    const int strength=std::clamp(g_shader.strength,0,100);
    return std::clamp(max_alpha*local*strength/10000,0,255);
}

bool shaderNeedsFrameTexture(VisualShader shader){
    switch(shader){
        case VisualShader::CRT:
        case VisualShader::Terminal:
        case VisualShader::LCD:
        case VisualShader::DotMatrix:
        case VisualShader::Bloom:
        case VisualShader::Analog:
        case VisualShader::Chromatic:
        case VisualShader::Ghosting:
        case VisualShader::Arcade:
            return true;
        case VisualShader::None:
        case VisualShader::Scanlines:
        case VisualShader::Vignette:
        default:
            return false;
    }
}

bool shaderNeedsHistory(VisualShader shader){
    return shader==VisualShader::Terminal||shader==VisualShader::Ghosting;
}

void destroyTexture(SDL_Texture*& texture){
    if(texture){SDL_DestroyTexture(texture);texture=nullptr;}
}

void destroyPostTargets(){
    destroyTexture(g_post.frame);
    destroyTexture(g_post.blur);
    destroyTexture(g_post.history_a);
    destroyTexture(g_post.history_b);
    g_post.width=0;
    g_post.height=0;
    g_post.capturing=false;
    g_post.history_valid=false;
    g_post.blur_ready=false;
    g_post.warp_width=-1;
    g_post.warp_height=-1;
    g_post.warp_curve_key=-1;
    g_post.warp_alpha_key=-1;
}

SDL_Texture* makeTarget(SDL_Renderer* r,int w,int h,SDL_ScaleMode scale_mode=SDL_SCALEMODE_LINEAR){
    if(w<=0||h<=0)return nullptr;
    SDL_Texture* texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,w,h);
    if(!texture)return nullptr;
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture,scale_mode);
    return texture;
}

bool ensureFrameTarget(SDL_Renderer* r,int w,int h){
    if(g_post.renderer!=r){
        destroyPostTargets();
        g_post.renderer=r;
        g_post.target_unavailable=false;
    }
    if(g_post.target_unavailable)return false;
    if(g_post.frame&&g_post.width==w&&g_post.height==h)return true;
    destroyPostTargets();
    g_post.renderer=r;
    g_post.frame=makeTarget(r,w,h,SDL_SCALEMODE_LINEAR);
    if(!g_post.frame){
        g_post.target_unavailable=true;
        return false;
    }
    g_post.width=w;
    g_post.height=h;
    return true;
}

bool ensureBlurTarget(SDL_Renderer* r){
    if(g_post.blur)return true;
    const int bw=std::max(1,g_post.width/4);
    const int bh=std::max(1,g_post.height/4);
    g_post.blur=makeTarget(r,bw,bh,SDL_SCALEMODE_LINEAR);
    return g_post.blur!=nullptr;
}

bool ensureHistoryTargets(SDL_Renderer* r){
    if(g_post.history_a&&g_post.history_b)return true;
    destroyTexture(g_post.history_a);
    destroyTexture(g_post.history_b);
    const int hw=std::max(1,g_post.width/2);
    const int hh=std::max(1,g_post.height/2);
    g_post.history_a=makeTarget(r,hw,hh,SDL_SCALEMODE_LINEAR);
    g_post.history_b=makeTarget(r,hw,hh,SDL_SCALEMODE_LINEAR);
    g_post.history_valid=false;
    if(!g_post.history_a||!g_post.history_b){
        destroyTexture(g_post.history_a);
        destroyTexture(g_post.history_b);
        return false;
    }
    return true;
}

void resetTextureState(SDL_Texture* texture){
    if(!texture)return;
    SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(texture,255,255,255);
    SDL_SetTextureAlphaMod(texture,255);
}

void renderTextureCopy(SDL_Renderer* r,SDL_Texture* texture,const SDL_FRect* src,const SDL_FRect* dst,
                       SDL_BlendMode blend=SDL_BLENDMODE_BLEND,Uint8 alpha_mod=255,
                       Uint8 red=255,Uint8 green=255,Uint8 blue=255){
    if(!texture)return;
    SDL_SetTextureBlendMode(texture,blend);
    SDL_SetTextureColorMod(texture,red,green,blue);
    SDL_SetTextureAlphaMod(texture,alpha_mod);
    SDL_RenderTexture(r,texture,src,dst);
    resetTextureState(texture);
}

void clearTarget(SDL_Renderer* r,C color={0,0,0,255}){
    SDL_SetRenderDrawColor(r,color.r,color.g,color.b,color.a);
    SDL_RenderClear(r);
}

bool prepareBlur(SDL_Renderer* r){
    if(g_post.blur_ready&&g_post.blur)return true;
    if(!ensureBlurTarget(r))return false;
    SDL_SetRenderTarget(r,g_post.blur);
    SDL_SetRenderViewport(r,nullptr);
    SDL_SetRenderScale(r,1.0f,1.0f);
    clearTarget(r,{0,0,0,255});
    renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
    SDL_SetRenderTarget(r,nullptr);
    SDL_SetRenderViewport(r,nullptr);
    SDL_SetRenderScale(r,1.0f,1.0f);
    g_post.blur_ready=true;
    return true;
}

void applyScanlines(SDL_Renderer*r,int w,int h,int spacing,int thickness,int alpha){
    if(alpha<=0)return;
    spacing=std::max(2,spacing);thickness=std::clamp(thickness,1,std::max(1,spacing-1));
    static std::array<SDL_FRect,4096> rects{};
    int count=0;
    for(int y=0;y<h&&count<int(rects.size());y+=spacing)rects[static_cast<std::size_t>(count++)]={0,float(y),float(w),float(thickness)};
    if(count>0){const Uint8 a=static_cast<Uint8>(std::clamp(alpha,0,255));setRaw(r,{0,0,0,a});SDL_RenderFillRects(r,rects.data(),count);}
}

void applyPixelGrid(SDL_Renderer*r,int w,int h,int step,int thickness,int alpha){
    if(alpha<=0)return;
    step=std::max(4,step);thickness=std::clamp(thickness,1,std::max(1,step-1));
    static std::array<SDL_FRect,4096> vertical{};
    static std::array<SDL_FRect,4096> horizontal{};
    int vc=0,hc=0;
    for(int x=0;x<w&&vc<int(vertical.size());x+=step)vertical[static_cast<std::size_t>(vc++)]={float(x),0,float(thickness),float(h)};
    for(int y=0;y<h&&hc<int(horizontal.size());y+=step)horizontal[static_cast<std::size_t>(hc++)]={0,float(y),float(w),float(thickness)};
    if(vc>0){const Uint8 light=static_cast<Uint8>(std::clamp(alpha,0,255));setRaw(r,{255,255,255,light});SDL_RenderFillRects(r,vertical.data(),vc);}
    if(hc>0){const Uint8 dark=static_cast<Uint8>(std::clamp(alpha*3/4,0,255));setRaw(r,{0,0,0,dark});SDL_RenderFillRects(r,horizontal.data(),hc);}
}

void applyDotMask(SDL_Renderer*r,int w,int h,int spacing,int dot_size,int alpha){
    if(alpha<=0)return;
    spacing=std::max(4,spacing);dot_size=std::clamp(dot_size,1,std::max(1,spacing-1));
    const int mask=std::max(1,spacing-dot_size);
    static std::array<SDL_FRect,4096> rects{};
    int count=0;
    for(int x=dot_size;x<w&&count<int(rects.size());x+=spacing)rects[static_cast<std::size_t>(count++)]={float(x),0,float(mask),float(h)};
    for(int y=dot_size;y<h&&count<int(rects.size());y+=spacing)rects[static_cast<std::size_t>(count++)]={0,float(y),float(w),float(mask)};
    if(count>0){const Uint8 a=static_cast<Uint8>(std::clamp(alpha,0,255));setRaw(r,{0,0,0,a});SDL_RenderFillRects(r,rects.data(),count);}
}

void applyVignette(SDL_Renderer*r,int w,int h,int alpha,int radius,int softness){
    if(alpha<=0)return;
    const float reach=(1.0f-std::clamp(radius,0,100)/100.0f)*0.30f+0.035f;
    const float max_inset=std::min(w,h)*reach;
    const int layers=std::clamp(12+softness/3,12,44);
    const float thickness=std::max(1.0f,std::min(w,h)/720.0f);
    for(int i=0;i<layers;++i){
        const float outer=float(i)/layers;
        const float inset=max_inset*outer;
        const float falloff=1.0f-outer;
        const float shaped=falloff*falloff*(0.35f+0.65f*(softness/100.0f));
        const Uint8 a=static_cast<Uint8>(std::clamp(int(alpha*shaped),0,255));
        if(a==0)continue;
        fillAbs(r,inset,inset,float(w)-2*inset,thickness,{0,0,0,a});
        fillAbs(r,inset,float(h)-inset-thickness,float(w)-2*inset,thickness,{0,0,0,a});
        fillAbs(r,inset,inset,thickness,float(h)-2*inset,{0,0,0,a});
        fillAbs(r,float(w)-inset-thickness,inset,thickness,float(h)-2*inset,{0,0,0,a});
    }
}

void applyFlicker(SDL_Renderer*r,int w,int h,int alpha){
    if(alpha<=0)return;
    const Uint64 tick=SDL_GetTicks();
    const float phase=std::sin(float(tick%4000u)*0.021f)+0.45f*std::sin(float(tick%2100u)*0.063f);
    const int pulse=std::clamp(int(alpha*(0.4f+0.3f*phase)),0,alpha);
    if((tick/71u)%2u==0u)fillAbs(r,0,0,float(w),float(h),{255,255,255,static_cast<Uint8>(std::max(0,pulse/10))});
    else fillAbs(r,0,0,float(w),float(h),{0,0,0,static_cast<Uint8>(std::max(0,pulse/12))});
}

void prepareWarpMesh(int w,int h,int curvature,int strength,float alpha){
    const int curve_key=std::clamp(curvature,0,100)*101+std::clamp(strength,0,100);
    const int alpha_key=std::clamp(int(alpha*1000.0f+0.5f),0,1000);
    if(g_post.warp_width==w&&g_post.warp_height==h&&g_post.warp_curve_key==curve_key&&g_post.warp_alpha_key==alpha_key)return;

    if(!g_post.warp_indices_ready){
        int index=0;
        for(int y=0;y<kWarpRows;++y){
            for(int x=0;x<kWarpCols;++x){
                const int a=y*(kWarpCols+1)+x;
                const int b=a+1;
                const int c=a+(kWarpCols+1);
                const int d=c+1;
                g_post.warp_indices[static_cast<std::size_t>(index++)]=a;
                g_post.warp_indices[static_cast<std::size_t>(index++)]=b;
                g_post.warp_indices[static_cast<std::size_t>(index++)]=d;
                g_post.warp_indices[static_cast<std::size_t>(index++)]=a;
                g_post.warp_indices[static_cast<std::size_t>(index++)]=d;
                g_post.warp_indices[static_cast<std::size_t>(index++)]=c;
            }
        }
        g_post.warp_indices_ready=true;
    }

    const float curve=std::clamp(curvature,0,100)/100.0f;
    const float global=0.35f+0.65f*(std::clamp(strength,0,100)/100.0f);
    const float warp=0.22f*curve*global;
    const float margin=warp*0.34f;
    const float half_w=w*(0.5f-margin);
    const float half_h=h*(0.5f-margin);
    int vertex=0;
    for(int y=0;y<=kWarpRows;++y){
        const float v=float(y)/kWarpRows;
        const float ny=v*2.0f-1.0f;
        for(int x=0;x<=kWarpCols;++x){
            const float u=float(x)/kWarpCols;
            const float nx=u*2.0f-1.0f;
            const float bend_x=1.0f-warp*ny*ny;
            const float bend_y=1.0f-warp*nx*nx;
            SDL_Vertex& out=g_post.warp_vertices[static_cast<std::size_t>(vertex++)];
            out.position={w*0.5f+nx*half_w*bend_x,h*0.5f+ny*half_h*bend_y};
            out.color={1.0f,1.0f,1.0f,alpha};
            out.tex_coord={u,v};
        }
    }
    g_post.warp_width=w;
    g_post.warp_height=h;
    g_post.warp_curve_key=curve_key;
    g_post.warp_alpha_key=alpha_key;
}

void renderWarpedFrame(SDL_Renderer*r,int w,int h,float alpha=1.0f){
    prepareWarpMesh(w,h,g_shader.curvature,g_shader.strength,alpha);
    SDL_SetTextureBlendMode(g_post.frame,SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(r,g_post.frame,g_post.warp_vertices.data(),kWarpVertexCount,
                       g_post.warp_indices.data(),kWarpIndexCount);
    resetTextureState(g_post.frame);
}

void renderBloom(SDL_Renderer*r,int w,int h,int amount,int radius,int softness,int threshold,
                 C tint={255,255,255,255}){
    const int base_alpha=effectAlpha(amount,120);
    if(base_alpha<=0||!prepareBlur(r))return;
    const float threshold_factor=1.0f-0.70f*(std::clamp(threshold,0,100)/100.0f);
    const int alpha=std::clamp(int(base_alpha*threshold_factor),0,170);
    if(alpha<=0)return;
    const float spread=1.0f+std::clamp(radius,0,100)*0.12f;
    const int taps=std::clamp(4+softness/18,4,10);
    for(int i=0;i<taps;++i){
        const float angle=float(i)*6.28318530718f/taps;
        const float ring=(i%2==0?1.0f:0.55f)*spread;
        const float dx=std::cos(angle)*ring;
        const float dy=std::sin(angle)*ring;
        SDL_FRect dst{dx,dy,float(w),float(h)};
        renderTextureCopy(r,g_post.blur,nullptr,&dst,SDL_BLENDMODE_ADD,
                          static_cast<Uint8>(std::clamp(alpha/(2+(i%3)),0,255)),tint.r,tint.g,tint.b);
    }
    SDL_FRect center{0,0,float(w),float(h)};
    renderTextureCopy(r,g_post.blur,nullptr,&center,SDL_BLENDMODE_ADD,
                      static_cast<Uint8>(std::clamp(alpha/3,0,255)),tint.r,tint.g,tint.b);
}

void applySoftness(SDL_Renderer*r,int w,int h,int softness){
    const int alpha=effectAlpha(softness,115);
    if(alpha<=0||!prepareBlur(r))return;
    SDL_FRect dst{0,0,float(w),float(h)};
    renderTextureCopy(r,g_post.blur,nullptr,&dst,SDL_BLENDMODE_BLEND,static_cast<Uint8>(alpha));
}

void applyLcdSubpixels(SDL_Renderer*r,int w,int h,int alpha,int step){
    if(alpha<=0)return;
    step=std::max(6,step);
    const Uint8 a=static_cast<Uint8>(std::clamp(alpha,0,255));
    for(int x=0;x<w;x+=step){
        fillAbs(r,float(x),0,1,float(h),{255,55,55,a});
        fillAbs(r,float(x+std::max(1,step/3)),0,1,float(h),{55,255,90,a});
        fillAbs(r,float(x+std::max(2,2*step/3)),0,1,float(h),{55,135,255,a});
    }
}

void renderAnalogFrame(SDL_Renderer*r,int w,int h){
    const Uint64 tick=SDL_GetTicks();
    const float jitter_px=(g_shader.horizontal_jitter/100.0f)*std::max(2.0f,w*0.018f)*g_shader.strength/100.0f;
    const float distortion=g_shader.distortion/100.0f*g_shader.strength/100.0f;
    const int strip_h=std::clamp(18-int(distortion*14.0f),4,18);
    SDL_SetTextureBlendMode(g_post.frame,SDL_BLENDMODE_NONE);
    for(int y=0;y<h;y+=strip_h){
        const int sh=std::min(strip_h,h-y);
        const float wave=std::sin(float(y)*0.031f+float(tick%2000u)*0.006f);
        float dx=wave*jitter_px;
        if(distortion>0.05f&&((y/strip_h+int(tick/73u))%29)==0)dx+=(distortion*w*0.045f)*(wave>=0?1.0f:-1.0f);
        const float stretch=distortion*std::abs(wave)*w*0.006f;
        SDL_FRect src{0,float(y),float(w),float(sh)};
        SDL_FRect dst{dx,float(y),float(w)+stretch,float(sh)};
        SDL_RenderTexture(r,g_post.frame,&src,&dst);
    }
    resetTextureState(g_post.frame);
}

void applyAnalogNoise(SDL_Renderer*r,int w,int h){
    const Uint64 tick=SDL_GetTicks();
    const int noise_alpha=effectAlpha(g_shader.noise,70);
    const int distortion_alpha=effectAlpha(g_shader.distortion,95);
    const int count=3+std::clamp(g_shader.noise,0,100)/7;
    for(int i=0;i<count;++i){
        const int y=int((tick/7u+Uint64(i*53))%Uint64(std::max(1,h-2)));
        const int len=50+int((tick/11u+Uint64(i*97))%Uint64(std::max(51,w/2)));
        const int x=int((tick/13u+Uint64(i*131))%Uint64(std::max(1,w-len)));
        fillAbs(r,float(x),float(y),float(len),1,{255,255,255,static_cast<Uint8>(std::clamp(noise_alpha,0,255))});
    }
    for(int i=0;i<2+g_shader.distortion/20;++i){
        const int y=int((tick/37u+Uint64(i*101))%Uint64(std::max(1,h-4)));
        const int hh=1+g_shader.distortion/35;
        fillAbs(r,0,float(y),float(w),float(hh),{0,0,0,static_cast<Uint8>(std::clamp(distortion_alpha,0,255))});
    }
}

void renderChromaticFrame(SDL_Renderer*r,int w,int h){
    const int pixels=std::max(0,g_shader.rgb_offset);
    const float scale=0.45f+0.55f*g_shader.strength/100.0f;
    const float off=pixels*scale;
    float dx=0.0f,dy=0.0f;
    switch(g_shader.direction&3){
        case 0:dx=off;break;
        case 1:dy=off;break;
        case 2:dx=off*0.7071f;dy=off*0.7071f;break;
        case 3:dx=off*0.7071f;dy=-off*0.7071f;break;
    }
    SDL_FRect red{-dx,-dy,float(w),float(h)};
    SDL_FRect green{0,0,float(w),float(h)};
    SDL_FRect blue{dx,dy,float(w),float(h)};
    renderTextureCopy(r,g_post.frame,nullptr,&red,SDL_BLENDMODE_ADD,255,255,0,0);
    renderTextureCopy(r,g_post.frame,nullptr,&green,SDL_BLENDMODE_ADD,255,0,255,0);
    renderTextureCopy(r,g_post.frame,nullptr,&blue,SDL_BLENDMODE_ADD,255,0,0,255);
}

void renderHistoryTrail(SDL_Renderer*r,int w,int h,int persistence,int trails,C tint={255,255,255,255}){
    if(!g_post.history_valid||!g_post.history_a)return;
    const int base=effectAlpha(persistence,105);
    if(base<=0)return;
    const int count=std::clamp(trails,1,8);
    const float distance=0.75f+std::clamp(persistence,0,100)*0.035f;
    for(int i=count-1;i>=0;--i){
        const float offset=(i+1)*distance;
        SDL_FRect dst{offset,offset*0.35f,float(w),float(h)};
        const int alpha=std::clamp(base/(2+i),0,100);
        renderTextureCopy(r,g_post.history_a,nullptr,&dst,SDL_BLENDMODE_ADD,static_cast<Uint8>(alpha),tint.r,tint.g,tint.b);
    }
}

void updateHistory(SDL_Renderer*r,int persistence){
    if(!ensureHistoryTargets(r))return;
    SDL_SetRenderTarget(r,g_post.history_b);
    SDL_SetRenderViewport(r,nullptr);
    SDL_SetRenderScale(r,1.0f,1.0f);
    clearTarget(r,{0,0,0,255});
    if(!g_post.history_valid){
        renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
    }else{
        const int p=std::clamp(persistence,0,100);
        const Uint8 old_alpha=static_cast<Uint8>(std::clamp(35+p*2,35,225));
        const Uint8 current_alpha=static_cast<Uint8>(std::clamp(235-p*17/10,65,235));
        renderTextureCopy(r,g_post.history_a,nullptr,nullptr,SDL_BLENDMODE_BLEND,old_alpha);
        renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_ADD,current_alpha);
    }
    SDL_SetRenderTarget(r,nullptr);
    SDL_SetRenderViewport(r,nullptr);
    SDL_SetRenderScale(r,1.0f,1.0f);
    std::swap(g_post.history_a,g_post.history_b);
    g_post.history_valid=true;
}

void applySimpleOverlayShader(SDL_Renderer*r,int w,int h){
    switch(g_shader.mode){
        case VisualShader::Scanlines:
            applyScanlines(r,w,h,g_shader.scanline_spacing,g_shader.line_thickness,effectAlpha(100,110));
            break;
        case VisualShader::Vignette:
            applyVignette(r,w,h,effectAlpha(100,145),g_shader.radius,g_shader.softness);
            break;
        default:
            break;
    }
}
}

void beginVisualShaderFrame(SDL_Renderer*r){
    g_post.capturing=false;
    g_post.blur_ready=false;
    if(!r||g_shader.mode==VisualShader::None||g_shader.strength<=0)return;
    if(!shaderNeedsFrameTexture(g_shader.mode)){
        g_post.history_valid=false;
        return;
    }
    int w=0,h=0;
    if(!SDL_GetRenderOutputSize(r,&w,&h)||w<=0||h<=0)return;
    if(!ensureFrameTarget(r,w,h))return;
    if(!shaderNeedsHistory(g_shader.mode))g_post.history_valid=false;
    if(!SDL_SetRenderTarget(r,g_post.frame))return;
    SDL_SetRenderViewport(r,nullptr);
    SDL_SetRenderScale(r,1.0f,1.0f);
    g_post.capturing=true;
}

void applyVisualShader(SDL_Renderer*r){
    if(!r||g_shader.mode==VisualShader::None||g_shader.strength<=0)return;
    int w=960,h=720;
    if(!SDL_GetRenderOutputSize(r,&w,&h)||w<=0||h<=0){w=960;h=720;}

    if(!g_post.capturing){
        SDL_SetRenderTarget(r,nullptr);
        SDL_SetRenderViewport(r,nullptr);
        SDL_SetRenderScale(r,1.0f,1.0f);
        applySimpleOverlayShader(r,w,h);
        return;
    }

    SDL_SetRenderTarget(r,nullptr);
    SDL_SetRenderViewport(r,nullptr);
    SDL_SetRenderScale(r,1.0f,1.0f);
    clearTarget(r,{0,0,0,255});

    switch(g_shader.mode){
        case VisualShader::None:
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
            break;
        case VisualShader::CRT:{
            const int soft=effectAlpha(g_shader.softness,105);
            renderWarpedFrame(r,w,h,soft>0?std::clamp(1.0f-soft/420.0f,0.72f,1.0f):1.0f);
            if(soft>0)applySoftness(r,w,h,g_shader.softness);
            renderBloom(r,w,h,g_shader.glow,45+g_shader.glow/2,g_shader.softness,38);
            applyScanlines(r,w,h,g_shader.scanline_spacing,1,effectAlpha(g_shader.scanlines,105));
            applyVignette(r,w,h,effectAlpha(g_shader.vignette,150),50,g_shader.softness);
            break;
        }
        case VisualShader::Terminal:
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
            renderBloom(r,w,h,g_shader.glow,35+g_shader.glow/2,55,45,{155,255,180,255});
            renderHistoryTrail(r,w,h,g_shader.persistence,g_shader.trail_length,{140,255,175,255});
            applyScanlines(r,w,h,4,1,effectAlpha(g_shader.scanlines,90));
            applyFlicker(r,w,h,effectAlpha(g_shader.flicker,95));
            updateHistory(r,g_shader.persistence);
            break;
        case VisualShader::LCD:{
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
            if(g_shader.softness>0)applySoftness(r,w,h,g_shader.softness);
            if(g_shader.pixel_grid>0)applyPixelGrid(r,w,h,g_shader.grid_size,g_shader.line_thickness,effectAlpha(g_shader.pixel_grid,75));
            if(g_shader.subpixel>0)applyLcdSubpixels(r,w,h,effectAlpha(g_shader.subpixel,60),g_shader.grid_size);
            break;
        }
        case VisualShader::DotMatrix:
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_ADD,
                              static_cast<Uint8>(effectAlpha(g_shader.dot_brightness,40)));
            applyDotMask(r,w,h,g_shader.dot_spacing,g_shader.dot_size,effectAlpha(100,245));
            break;
        case VisualShader::Bloom:
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
            renderBloom(r,w,h,100,g_shader.radius,g_shader.softness,g_shader.threshold);
            break;
        case VisualShader::Scanlines:
        case VisualShader::Vignette:
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
            applySimpleOverlayShader(r,w,h);
            break;
        case VisualShader::Analog:
            renderAnalogFrame(r,w,h);
            applyAnalogNoise(r,w,h);
            applyFlicker(r,w,h,effectAlpha(g_shader.flicker,85));
            break;
        case VisualShader::Chromatic:
            renderChromaticFrame(r,w,h);
            break;
        case VisualShader::Ghosting:
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
            renderHistoryTrail(r,w,h,g_shader.persistence,g_shader.trail_length);
            updateHistory(r,g_shader.persistence);
            break;
        case VisualShader::Arcade:
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
            renderBloom(r,w,h,g_shader.bloom,45+g_shader.bloom/2,45,45);
            applyScanlines(r,w,h,4,1,effectAlpha(g_shader.scanlines,82));
            applyPixelGrid(r,w,h,8,1,effectAlpha(g_shader.pixel_grid,36));
            applyVignette(r,w,h,effectAlpha(g_shader.vignette,105),58,52);
            break;
        default:
            renderTextureCopy(r,g_post.frame,nullptr,nullptr,SDL_BLENDMODE_NONE,255);
            break;
    }
    g_post.capturing=false;
}

void shutdownVisualShaderPipeline(){
    destroyPostTargets();
    g_post.renderer=nullptr;
    g_post.target_unavailable=false;
}

void renderGame(SDL_Renderer*r,Game&g,const RenderInfo&i){
    const auto canvas=beginCanvas(r,false);
    set(r,{11,14,20,255});SDL_RenderClear(r);

    // The board is the visual anchor. Keep its physical size tied to the
    // available height, but use extra width for the surrounding information
    // instead of forcing the whole app into a 4:3 letterbox.
    const float cell=30.0f;
    const float board_w=cell*kBoardW;
    const float board_h=cell*kVisibleH;
    const float extra_h=std::max(0.0f,canvas.h-720.0f);
    const float bx=std::max(330.0f,canvas.w*0.5f-65.0f);
    const float by=60.0f+extra_h*0.5f;
    const float stats_x=std::max(20.0f,bx-390.0f);
    const float hold_x=bx-150.0f;
    const float next_x=bx+board_w+25.0f;

    // playfield
    fill(r,bx-4,by-4,board_w+8,board_h+8,{30,35,45,255});
    fill(r,bx,by,board_w,board_h,{14,18,24,255});
    for(int yy=0;yy<kVisibleH;++yy)for(int x=0;x<kBoardW;++x){int y=yy+kHiddenH;auto p=g.board().cell(x,y);if(p!=Piece::None)drawTexturedCell(r,bx+x*cell,by+yy*cell,cell,color(p));}
    if(g.active().piece!=Piece::None){
        if(g.rules().ghost){auto a=g.active();a.y=g.ghostY();for(auto b:blocks(a.piece,a.rot)){int y=a.y+b.y-kHiddenH;if(y>=0){auto c=color(a.piece);c.a=85;outlinePiece(r,bx+(a.x+b.x)*cell+3,by+y*cell+3,cell-6,cell-6,c);}}}
        auto&a=g.active();for(auto b:blocks(a.piece,a.rot)){int y=a.y+b.y-kHiddenH;if(y>=0&&y<kVisibleH)drawTexturedCell(r,bx+(a.x+b.x)*cell,by+y*cell,cell,color(a.piece));}
    }
    txt(r,bx,by-38,"FASTRIS",{235,240,248,255},true);
    txt(r,bx+120,by-34,std::string(modeName(g.mode())),{130,210,255,255},true);

    // hold + next
    txt(r,hold_x+10,by+12,"HOLD",{145,155,170,255},true);
    outline(r,hold_x,by+45,120,90,{45,55,70,255});
    drawMini(r,g.holdPiece(),hold_x+60,by+90,20);
    txt(r,next_x+10,by+12,"NEXT",{145,155,170,255},true);
    auto n=g.next(std::max(1,g.rules().next_count));
    const std::size_t shown=std::min<std::size_t>(8,n.size());
    const float next_step=shown>6?62.0f:82.0f;
    const float next_h=shown>6?54.0f:72.0f;
    const float next_piece_size=shown>6?13.0f:16.0f;
    for(std::size_t k=0;k<shown;++k){float y=by+45+next_step*k;outline(r,next_x,y,125,next_h,{40,48,62,255});drawMini(r,n[k],next_x+62,y+next_h/2,next_piece_size);}

    // stats
    const auto&s=g.stats();double sec=s.elapsed_us/1e6;double rate_sec=std::max(0.001,sec);double pps=s.pieces/rate_sec,apm=s.attacks/rate_sec*60.0,kpp=s.pieces?double(s.inputs)/s.pieces:0.0;
    txt(r,stats_x,by+20,"RUN",{145,155,170,255},true);txtf(r,stats_x,by+56,{220,225,232,255},"SEED  %llu",(unsigned long long)i.seed);txtf(r,stats_x,by+76,{220,225,232,255},"TIME  %7.3f",sec);txtf(r,stats_x,by+96,{220,225,232,255},"SCORE %lld",(long long)s.score);txtf(r,stats_x,by+116,{220,225,232,255},"LINES %d",s.lines);txtf(r,stats_x,by+136,{220,225,232,255},"PPS   %.2f",pps);txtf(r,stats_x,by+156,{220,225,232,255},"APM   %.1f",apm);txtf(r,stats_x,by+176,{220,225,232,255},"KPP   %.2f",kpp);txtf(r,stats_x,by+196,{220,225,232,255},"FIN   %d",s.finesse_faults);txtf(r,stats_x,by+216,{220,225,232,255},"B2B   %d",s.b2b_chain);txtf(r,stats_x,by+236,{220,225,232,255},"COMBO %d",std::max(0,s.combo));
    auto cn=clearName(g.lastClear());if(*cn)txt(r,stats_x,by+270,cn,{255,205,100,255},true);if(g.lastAttack()>0)txtf(r,stats_x,by+300,{255,130,100,255},"ATTACK +%d",g.lastAttack());

    // Mode-specific progress: keep the information the player actually cares about visible.
    switch(g.mode()){
        case Mode::Sprint40:
        case Mode::SeedRace:
            txtf(r,stats_x,by+330,{145,155,170,255},"GOAL  40 LINES");
            txtf(r,stats_x,by+348,{130,210,255,255},"LEFT  %d",std::max(0,40-s.lines));
            break;
        case Mode::Ultra120:{
            const double left=std::max(0.0,120.0-sec);
            txt(r,stats_x,by+330,"GOAL  MAX SCORE",{145,155,170,255});
            txtf(r,stats_x,by+348,{130,210,255,255},"LEFT  %6.3f",left);
            break;
        }
        case Mode::Marathon:
            txtf(r,stats_x,by+330,{145,155,170,255},"LEVEL %d / 15",g.level());
            txtf(r,stats_x,by+348,{130,210,255,255},"LEFT  %d LINES",std::max(0,150-s.lines));
            break;
        case Mode::Cheese40:
            txtf(r,stats_x,by+330,{145,155,170,255},"CHEESE %d / 40",std::min(40,s.garbage_lines_cleared));
            txtf(r,stats_x,by+348,{130,210,255,255},"LEFT   %d",std::max(0,40-s.garbage_lines_cleared));
            break;
        case Mode::Finesse:{
            const double acc=s.pieces?100.0*double(s.finesse_perfect_pieces)/s.pieces:100.0;
            txtf(r,stats_x,by+330,{145,155,170,255},"FINESSE %.1f%%",acc);
            txtf(r,stats_x,by+348,{130,210,255,255},"FAULTS %d  STREAK %d",s.finesse_faults,s.finesse_streak);
            break;
        }
        case Mode::Zen:
            txt(r,stats_x,by+330,"ZEN   ENDLESS",{145,155,170,255});
            txt(r,stats_x,by+348,"NO AUTO GRAVITY",{130,210,255,255});
            break;
        case Mode::Custom:{
            const auto& cr=g.rules();
            if(cr.custom_line_goal>0)txtf(r,stats_x,by+330,{145,155,170,255},"LINES LEFT %d",std::max(0,cr.custom_line_goal-s.lines));
            else txt(r,stats_x,by+330,"LINES  ENDLESS",{145,155,170,255});
            if(cr.custom_time_limit_s>0)txtf(r,stats_x,by+348,{130,210,255,255},"TIME LEFT %.3f",std::max(0.0,double(cr.custom_time_limit_s)-sec));
            else if(cr.custom_gravity_ms==0)txt(r,stats_x,by+348,"GRAVITY OFF",{130,210,255,255});
            else txtf(r,stats_x,by+348,{130,210,255,255},"GRAVITY %d MS/CELL",cr.custom_gravity_ms);
            break;
        }
    }

    if(i.show_inputs){
        txt(r,stats_x,by+380,"INPUT",{145,155,170,255},true);
        int row=0;
        for(auto it=i.recent_inputs.rbegin();it!=i.recent_inputs.rend()&&row<7;++it,++row)actionLabel(r,stats_x,by+415+row*18,*it);
    }
    txt(r,stats_x,by+580,"F5 restart  F6 save replay",{120,130,145,255});txt(r,stats_x,by+598,"P pause  ESC menu",{120,130,145,255});
    if(i.replay_mode){txtf(r,next_x,by+586,{125,220,170,255},"REPLAY %.1fx%s",i.replay_speed,i.replay_paused?" PAUSED":"");txt(r,next_x,by+604,"SPACE pause  arrows seek",{120,130,145,255});}
    if(!i.status.empty())txt(r,bx,by+620,i.status,{120,220,160,255},true);
    if(i.paused){fill(r,bx,by+240,board_w,90,{5,8,12,220});txt(r,bx+80,by+270,"PAUSED",{255,255,255,255},true);}
    if(g.gameOver()){
        fill(r,bx,by+220,board_w,135,{5,8,12,235});txt(r,bx+60,by+240,"GAME OVER",{255,110,110,255},true);txtf(r,bx+55,by+278,{220,225,232,255},"TIME %.3f   LINES %d",sec,s.lines);txt(r,bx+52,by+312,"F5 RETRY   ESC MENU",{180,190,205,255});
    } else if(g.complete()){
        fill(r,bx,by+208,board_w,160,{5,8,12,235});txt(r,bx+55,by+226,"RUN COMPLETE",{120,240,170,255},true);
        switch(g.mode()){
            case Mode::Sprint40:
            case Mode::SeedRace:
                txtf(r,bx+44,by+266,{220,225,232,255},"TIME %.3f   PPS %.2f",sec,pps);
                txtf(r,bx+44,by+288,{220,225,232,255},"KPP %.2f   FIN %d",kpp,s.finesse_faults);
                break;
            case Mode::Ultra120:
                txtf(r,bx+44,by+266,{220,225,232,255},"SCORE %lld",(long long)s.score);
                txtf(r,bx+44,by+288,{220,225,232,255},"LINES %d   PPS %.2f",s.lines,pps);
                break;
            case Mode::Marathon:
                txtf(r,bx+44,by+266,{220,225,232,255},"SCORE %lld",(long long)s.score);
                txtf(r,bx+44,by+288,{220,225,232,255},"TIME %.3f   LINES %d",sec,s.lines);
                break;
            case Mode::Cheese40:
                txtf(r,bx+44,by+266,{220,225,232,255},"TIME %.3f   PPS %.2f",sec,pps);
                txtf(r,bx+44,by+288,{220,225,232,255},"CHEESE %d   FIN %d",s.garbage_lines_cleared,s.finesse_faults);
                break;
            case Mode::Custom:
                txtf(r,bx+44,by+266,{220,225,232,255},"SCORE %lld   LINES %d",(long long)s.score,s.lines);
                txtf(r,bx+44,by+288,{220,225,232,255},"TIME %.3f   PPS %.2f",sec,pps);
                break;
            case Mode::Finesse:
            case Mode::Zen:
                txtf(r,bx+44,by+266,{220,225,232,255},"TIME %.3f   PIECES %d",sec,s.pieces);
                txtf(r,bx+44,by+288,{220,225,232,255},"FIN %d   PPS %.2f",s.finesse_faults,pps);
                break;
        }
        txt(r,bx+52,by+330,"F5 RETRY   ESC MENU",{180,190,205,255});
    }
}

void renderMenu(SDL_Renderer*r,int sel,bool tournament){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);
    txt(r,350,45,"FASTRIS",{235,240,248,255},true);
    txt(r,454,50,std::string("v")+fasttris::kVersion,{120,135,155,255});
    txt(r,276,78,"DETERMINISTIC COMPETITIVE BLOCK ENGINE",{120,135,155,255});
    static const std::array<const char*,11> items={
        "SPRINT 40L","ULTRA 2:00","MARATHON 150L","ZEN","CHEESE RACE 40",
        "FINESSE TRAINER","SEED RACE 40L","SANDBOX","SETTINGS","REPLAYS","QUIT"
    };
    for(int n=0;n<(int)items.size();++n){
        C c=n==sel?C{120,220,255,255}:C{205,210,220,255};
        txt(r,310,122+n*34,(n==sel?"> ":"  ")+std::string(items[n]),c,true);
    }
    static const std::array<const char*,8> desc={
        "Clear 40 lines as fast as possible.",
        "Score as high as possible in exactly two minutes.",
        "Clear 150 lines while gravity increases every 10 lines.",
        "Relaxed practice with no automatic gravity.",
        "Clear 40 garbage lines while the cheese replenishes.",
        "No-gravity technique trainer; minimize finesse faults.",
        "Standardized 40L rules; same seed means the same piece order.",
        "Configure gravity, goals, timer and starting garbage."
    };
    if(sel<8){
        txt(r,245,520,"MODE",{120,135,155,255},true);
        txt(r,245,546,desc[sel],{190,198,210,255});
    }
    txt(r,245,600,"Tournament lock",{130,140,155,255});
    txt(r,385,600,tournament?"ON":"OFF",tournament?C{110,230,160,255}:C{170,175,185,255});
    txt(r,245,632,"ENTER select   arrows navigate   H help",{130,140,155,255});
}

void renderReplayMenu(SDL_Renderer*r,int sel,bool hasLast,const std::string&status){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);
    txt(r,330,70,"REPLAYS",{235,240,248,255},true);
    std::array<std::string,3> items={"PLAY LAST REPLAY","LOAD REPLAY FILE","BACK"};
    if(!hasLast)items[0]+="  [NONE]";
    for(int n=0;n<3;++n){
        const bool unavailable=n==0&&!hasLast;
        C color=n==sel?C{120,220,255,255}:unavailable?C{105,112,125,255}:C{210,215,225,255};
        txt(r,310,190+n*64,(n==sel?"> ":"  ")+items[n],color,true);
    }
    txt(r,250,430,"Saved replay files use the .ftr format.",{155,168,185,255});
    txt(r,250,458,"Loading verifies the same deterministic replay format used by the viewer.",{155,168,185,255});
    if(!status.empty())txt(r,250,520,status,{255,205,100,255},true);
    txt(r,250,610,"UP/DOWN select   ENTER open   ESC back",{135,145,160,255});
}

void renderSettings(SDL_Renderer*r,const AppConfig&c,int sel,bool editing,const std::string&editText,const std::string&status){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);
    txt(r,330,18,"SETTINGS",{235,240,248,255},true);

    auto h=c.rules.handling;
    bool ghost=c.rules.ghost;
    int next=c.rules.next_count;
    if(c.rules.tournament){
        h.lock_delay_ms=500;h.max_lock_resets=15;h.allow_180=false;h.irs=true;h.ihs=true;ghost=true;next=5;
    }

    const bool blink=(SDL_GetTicks()/350)%2==0;
    auto number=[&](int item,const std::string&normal){
        if(!editing||sel!=item)return normal;
        return blink?editText:std::string(editText.size(),' ');
    };

    std::array<std::string,SettingCount> v={
        "DAS             "+number(SettingDas,std::to_string(h.das_ms))+" ms",
        "ARR             "+number(SettingArr,std::to_string(h.arr_ms))+" ms"+(h.arr_ms==0&&!(editing&&sel==SettingArr)?" (INSTANT)":""),
        "SDF             "+number(SettingSdf,std::to_string(h.sdf))+"x"+(h.sdf==0&&!(editing&&sel==SettingSdf)?" (SONIC)":""),
        "DCD             "+number(SettingDcd,std::to_string(h.dcd_ms))+" ms",
        "LOCK            "+number(SettingLock,std::to_string(h.lock_delay_ms))+" ms",
        "RESETS          "+number(SettingResets,std::to_string(h.max_lock_resets)),
        std::string("180 ROT         ")+(h.allow_180?"ON":"OFF"),
        std::string("IRS             ")+(h.irs?"ON":"OFF"),
        std::string("IHS             ")+(h.ihs?"ON":"OFF"),
        std::string("GHOST           ")+(ghost?"ON":"OFF"),
        "NEXT            "+number(SettingNext,std::to_string(next)),
        std::string("SHOW INPUTS     ")+(c.show_inputs?"ON":"OFF"),
        std::string("VSYNC           ")+(c.vsync?"ON":"OFF"),
        "FPS CAP         "+number(SettingFpsCap,c.fps_cap==0?"0":std::to_string(c.fps_cap))+(c.fps_cap==0&&!(editing&&sel==SettingFpsCap)?" (UNCAPPED)":""),
        std::string("TOURNAMENT      ")+(c.rules.tournament?"ON":"OFF"),
        "SEED SETTINGS",
        "CONTROLS",
        "MISCELLANEOUS",
        "RESET SETTINGS"
    };

    for(int n=0;n<SettingCount;++n){
        const bool locked=c.rules.tournament&&n>=SettingLock&&n<=SettingNext;
        const bool action=n>=SettingSeedMenu;
        C normal=action?C{190,198,210,255}:C{210,215,225,255};
        if(n==SettingReset)normal={245,180,110,255};
        std::string label=(n==sel?"> ":"  ")+v[n]+(locked?"  [LOCKED]":"");
        txt(r,238,54+n*25,label,n==sel?C{120,220,255,255}:locked?C{150,150,155,255}:normal,true);
    }

    static const std::array<const char*,SettingCount> hint={
        "DAS: delay before held horizontal movement begins repeating.",
        "ARR: repeat interval after DAS. 0 intentionally shifts to the wall.",
        "SDF: soft-drop speed multiplier. 0 performs a sonic drop.",
        "DCD: reduces DAS charge when changing horizontal direction.",
        "Lock delay before a grounded piece locks.",
        "Maximum grounded move/rotation lock-delay resets.",
        "Optional 180-degree rotation.",
        "IRS: held rotation is applied to the next spawning piece.",
        "IHS: held Hold is applied to the next spawning piece.",
        "Show or hide the landing ghost.",
        "Number of upcoming pieces displayed, from 1 to 8.",
        "Show or hide the live input history while playing and watching replays.",
        "Synchronize presentation to the display refresh.",
        "Render cap when VSync is disabled. Enter 0 for uncapped.",
        "Apply the locked competitive ruleset during runs.",
        "Open seed setup, randomize, copy and paste tools.",
        "Open the dedicated keyboard and gamepad rebinding screen.",
        "Graphics area for shaders, textures and the active presentation palette.",
        "Restore gameplay/display preferences. Seed and controls stay unchanged."
    };

    if(c.rules.tournament)txt(r,220,560,"TOURNAMENT LOCK: placement rules marked [LOCKED] are fixed during runs",{255,190,100,255});
    if(!status.empty())txt(r,220,586,status,{255,205,100,255},true);
    txt(r,220,612,hint[std::clamp(sel,0,SettingCount-1)],{150,165,182,255});
    txt(r,220,646,editing?"TYPE NUMBER   ENTER apply   ESC cancel":"ENTER edit/open/toggle   LEFT/RIGHT change   ESC save + back",{135,145,160,255});
}

void renderSeedSettings(SDL_Renderer*r,std::uint64_t seed,int sel,bool editing,const std::string&editText,const std::string&status){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);
    txt(r,330,55,"SEED SETTINGS",{235,240,248,255},true);

    const bool blink=(SDL_GetTicks()/350)%2==0;
    const std::string seedText=editing
        ? (blink?editText:std::string(editText.size(),' '))
        : std::to_string(seed);

    std::array<std::string,SeedSettingCount> items={
        "SEED       "+seedText,
        "RANDOMIZE SEED",
        "COPY SEED",
        "PASTE SEED",
        "BACK"
    };

    for(int n=0;n<SeedSettingCount;++n){
        const C normal=n==SeedSettingBack?C{170,180,195,255}:C{210,215,225,255};
        txt(r,245,175+n*62,(n==sel?"> ":"  ")+items[n],n==sel?C{120,220,255,255}:normal,true);
    }

    txt(r,245,510,"Same seed = same deterministic piece sequence.",{155,168,185,255});
    txt(r,245,538,"Paste reads the current system clipboard and accepts a decimal uint64 seed.",{155,168,185,255});
    if(!status.empty())txt(r,245,580,status,{255,205,100,255},true);
    txt(r,245,615,editing?"TYPE NUMBER   ENTER apply   ESC cancel":"UP/DOWN select   ENTER use   ESC back",{135,145,160,255});
    if(!editing)txt(r,245,642,"CTRL/CMD+C copy   CTRL/CMD+V paste",{135,145,160,255});
}

void renderSandboxSetup(SDL_Renderer*r,const AppConfig&c,int sel){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);txt(r,350,55,"SANDBOX",{235,240,248,255},true);
    const auto& q=c.rules;
    auto gravity=q.custom_gravity_ms==0?std::string("OFF"):std::to_string(q.custom_gravity_ms)+" ms/cell";
    auto lines=q.custom_line_goal==0?std::string("ENDLESS"):std::to_string(q.custom_line_goal);
    auto timer=q.custom_time_limit_s==0?std::string("OFF"):std::to_string(q.custom_time_limit_s)+" s";
    std::array<std::string,5> items={
        "GRAVITY        "+gravity,
        "LINE GOAL      "+lines,
        "TIME LIMIT     "+timer,
        "START GARBAGE  "+std::to_string(q.custom_start_garbage),
        "START RUN"
    };
    for(int n=0;n<(int)items.size();++n){txt(r,290,160+n*62,(n==sel?"> ":"  ")+items[n],n==sel?C{120,220,255,255}:C{210,215,225,255},true);}
    txt(r,230,500,"Sandbox runs are deterministic: the seed controls pieces and starting garbage.",{155,168,185,255});
    txt(r,230,530,"Gravity 0 disables automatic fall. Goal/time 0 means endless/off.",{155,168,185,255});
    txt(r,230,590,"UP/DOWN select   LEFT/RIGHT change",{135,145,160,255});
    txt(r,230,612,"ENTER start   ESC back",{135,145,160,255});
}

void renderControls(SDL_Renderer*r,const AppConfig&c,int sel,bool rebinding,bool waitpad){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);
    txt(r,350,28,"CONTROLS",{235,240,248,255},true);

    static constexpr int grid[3][4]={
        {0,1,2,3},
        {4,5,6,7},
        {8,9,kControlResetIndex,-1}
    };
    static constexpr const char* rowNames[3]={"MOVEMENT","ROTATION","SYSTEM"};
    constexpr float x0=165.0f, step=195.0f, cardW=180.0f, cardH=112.0f;
    constexpr float ys[3]={135.0f,310.0f,485.0f};

    for(int row=0;row<3;++row){
        const float rowX0=x0+(row==2?step*0.5f:0.0f);
        txt(r,rowX0,ys[row]-28,rowNames[row],{120,135,155,255},true);
        for(int col=0;col<4;++col){
            const int idx=grid[row][col];
            if(idx<0)continue;
            const float x=rowX0+col*step;
            const bool selected=idx==sel;
            const C border=selected?C{120,220,255,255}:C{45,55,70,255};
            if(selected)fill(r,x,ys[row],cardW,cardH,{17,26,36,255});
            outline(r,x,ys[row],cardW,cardH,border);

            if(idx==kControlResetIndex){
                txt(r,x+18,ys[row]+24,"RESET CONTROLS",selected?C{120,220,255,255}:C{245,180,110,255});
                txt(r,x+18,ys[row]+66,"ENTER",{155,168,185,255});
                continue;
            }

            const auto a=static_cast<Action>(idx);
            std::string title(actionName(a));
            std::transform(title.begin(),title.end(),title.begin(),[](unsigned char ch){return static_cast<char>(std::toupper(ch));});
            txt(r,x+14,ys[row]+14,title,selected?C{120,220,255,255}:C{210,215,225,255},true);
            txt(r,x+14,ys[row]+55,"KEY  "+std::string(SDL_GetKeyName(c.keys[idx])),{185,195,208,255});
            txt(r,x+14,ys[row]+79,"PAD  "+std::string(padName(c.pads[idx])),{185,195,208,255});
        }
    }

    if(rebinding){
        txt(r,195,635,waitpad?"PRESS A GAMEPAD BUTTON (ESC CANCELS)":"PRESS A KEY (G = BIND GAMEPAD INSTEAD)",{255,205,100,255},true);
    }else if(sel==kControlResetIndex){
        txt(r,195,635,"ENTER restore defaults   ARROWS navigate   ESC back",{135,145,160,255});
    }else{
        txt(r,195,635,"ENTER rebind keyboard   G rebind gamepad   ARROWS navigate   ESC back",{135,145,160,255});
    }
}

void renderMiscellaneous(SDL_Renderer*r,const AppConfig&cfg,int sel,const std::string&status){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);
    txt(r,300,48,"MISCELLANEOUS",{235,240,248,255},true);
    txt(r,265,112,"GRAPHICS",{145,155,170,255},true);

    const std::array<std::string,kMiscItemCount> items={
        "SHADERS","TEXTURES","PALETTES","AFFECTS PIECES","RESET GRAPHICS"};
    for(int n=0;n<kMiscItemCount;++n){
        const float y=160.0f+n*58.0f;
        const bool selected=n==sel;
        const C normal=n==kMiscResetGraphicsIndex?C{245,180,110,255}:C{210,215,225,255};
        const C row_color=selected?C{120,220,255,255}:normal;
        if(n==kMiscPalettePiecesIndex){
            if(selected)txt(r,265,y,">",row_color,true);
            const float branch_x=300.0f;
            fill(r,branch_x,y+2.0f,3.0f,17.0f,row_color);
            fill(r,branch_x,y+16.0f,18.0f,3.0f,row_color);
            txt(r,326,y,items[n],row_color,true);
        }else{
            txt(r,265,y,(selected?"> ":"  ")+items[n],row_color,true);
        }

        const float value_x=650.0f;
        if(n==kMiscShadersIndex)txt(r,value_x,y,shaderName(cfg.shader),row_color,true);
        else if(n==kMiscTexturesIndex)txt(r,value_x,y,textureName(cfg.texture),row_color,true);
        else if(n==kMiscPalettesIndex)txt(r,value_x,y,paletteName(cfg.palette),row_color,true);
        else if(n==kMiscPalettePiecesIndex){
            const C toggle_color=selected?C{120,220,255,255}:(cfg.palette_affects_pieces?C{125,220,170,255}:C{235,150,125,255});
            txt(r,value_x,y,cfg.palette_affects_pieces?"ON":"OFF",toggle_color,true);
        }
    }
    if(sel==kMiscShadersIndex)txt(r,265,545,"ENTER open shader settings",{150,205,220,255},true);
    else if(sel==kMiscTexturesIndex)txt(r,265,545,"ENTER open procedural texture settings",{150,205,220,255},true);
    else if(sel==kMiscPalettesIndex)txt(r,265,545,"ENTER open palette settings",{150,205,220,255},true);
    else if(sel==kMiscPalettePiecesIndex)txt(r,265,545,"ENTER or LEFT/RIGHT toggle piece recoloring",{150,205,220,255},true);
    else txt(r,265,545,"ENTER restore all graphics presentation defaults",{245,180,110,255},true);
    if(!status.empty())txt(r,265,585,status,{255,205,100,255},true);
    txt(r,265,630,"UP/DOWN select   ENTER open/toggle   ESC back to Settings",{135,145,160,255});
}

void renderPaletteSettings(SDL_Renderer*r,const AppConfig&cfg,int sel){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);
    txt(r,330,34,"PALETTES",{235,240,248,255},true);
    txt(r,210,76,"Scrollable palette list. Changes preview and apply immediately.",{145,155,170,255});

    constexpr int visible_rows=6;
    constexpr float list_x=170.0f;
    constexpr float list_y=112.0f;
    constexpr float list_w=600.0f;
    constexpr float row_h=58.0f;
    constexpr float row_gap=6.0f;
    constexpr float list_h=visible_rows*row_h+(visible_rows-1)*row_gap;
    const int max_first=std::max(0,kVisualPaletteCount-visible_rows);
    const int desired_first=sel-visible_rows/2;
    const int first=std::clamp(desired_first,0,max_first);
    const int last=std::min(kVisualPaletteCount,first+visible_rows);

    fill(r,list_x-12.0f,list_y-12.0f,list_w+36.0f,list_h+24.0f,{13,18,25,255});
    outline(r,list_x-12.0f,list_y-12.0f,list_w+36.0f,list_h+24.0f,{42,54,68,255});

    for(int n=first;n<last;++n){
        const auto palette=static_cast<VisualPalette>(n);
        const bool selected=n==sel;
        const int row=n-first;
        const float y=list_y+row*(row_h+row_gap);
        if(selected)fill(r,list_x,y,list_w,row_h,{20,27,37,255});
        outline(r,list_x,y,list_w,row_h,selected?C{120,220,255,255}:C{45,55,70,255});
        txt(r,list_x+20.0f,y+14.0f,(selected?"> ":"  ")+std::string(paletteName(palette)),
            selected?C{120,220,255,255}:C{210,215,225,255},true);
    }

    // Compact scrollbar mirrors the automatically scrolled list window.
    constexpr float track_x=list_x+list_w+14.0f;
    fill(r,track_x,list_y,6.0f,list_h,{29,36,47,255});
    const float thumb_h=std::max(34.0f,list_h*(float(visible_rows)/float(kVisualPaletteCount)));
    const float travel=std::max(0.0f,list_h-thumb_h);
    const float fraction=max_first>0?float(first)/float(max_first):0.0f;
    fill(r,track_x-2.0f,list_y+travel*fraction,10.0f,thumb_h,{105,185,215,255});

    const auto selected_palette=static_cast<VisualPalette>(std::clamp(sel,0,kVisualPaletteCount-1));
    txt(r,170,530,std::string("SELECTED: ")+paletteName(selected_palette),{155,205,220,255},true);
    txt(r,170,578,"UP/DOWN scroll   LEFT/RIGHT page   Mouse wheel scrolls",{135,145,160,255});
    txt(r,170,606,"HOME/END jump   ENTER/ESC back",{135,145,160,255});
}

void renderTextureSettings(SDL_Renderer*r,const AppConfig&cfg,int sel){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);
    txt(r,330,24,"TEXTURES",{235,240,248,255},true);
    txt(r,165,62,"Texture controls stay on the left; the live preview has its own unobstructed panel.",{145,155,170,255});

    const auto controls=textureControls(cfg.texture);
    const int back_index=static_cast<int>(controls.size())+1;
    const int item_count=back_index+1;
    const float first_y=108.0f;
    const float available_h=420.0f;
    const float step=std::clamp(available_h/std::max(1,item_count),40.0f,54.0f);
    const float frame_h=std::min(44.0f,step-3.0f);
    constexpr float controls_x=70.0f;
    constexpr float controls_w=500.0f;

    auto drawRow=[&](int index,const std::string&label,const std::string&value,bool accent=false){
        const float y=first_y+step*index;
        const bool selected=index==sel;
        if(selected)fill(r,controls_x,y-7,controls_w,frame_h,{20,27,37,255});
        outline(r,controls_x,y-7,controls_w,frame_h,selected?C{120,220,255,255}:C{45,55,70,255});
        const C label_color=selected?C{120,220,255,255}:accent?C{245,180,110,255}:C{210,215,225,255};
        txt(r,controls_x+20,y+5,(selected?"> ":"  ")+label,label_color,true);
        if(!value.empty())txt(r,controls_x+350,y+8,value,selected?C{150,230,255,255}:C{155,205,220,255});
    };

    drawRow(0,"TEXTURE",textureName(cfg.texture));
    for(std::size_t i=0;i<controls.size();++i)
        drawRow(static_cast<int>(i)+1,textureControlName(controls[i]),textureControlValueText(cfg,controls[i]));
    drawRow(back_index,"BACK","",true);

    // Dedicated preview card: rows can never draw over this region.
    constexpr float panel_x=600.0f;
    constexpr float panel_y=104.0f;
    constexpr float panel_w=290.0f;
    constexpr float panel_h=430.0f;
    fill(r,panel_x,panel_y,panel_w,panel_h,{14,19,27,255});
    outline(r,panel_x,panel_y,panel_w,panel_h,{58,72,90,255});
    txt(r,panel_x+28,panel_y+24,"LIVE PREVIEW",{145,155,170,255},true);
    fill(r,panel_x+20,panel_y+68,panel_w-40,panel_h-110,{8,11,16,255});
    outline(r,panel_x+20,panel_y+68,panel_w-40,panel_h-110,{42,53,68,255});

    const float ps=34.0f;
    const float tx=panel_x+42.0f;
    const float ty=panel_y+190.0f;
    drawTexturedCell(r,tx,ty,ps,color(Piece::T));
    drawTexturedCell(r,tx+ps,ty,ps,color(Piece::T));
    drawTexturedCell(r,tx+ps*2,ty,ps,color(Piece::T));
    drawTexturedCell(r,tx+ps,ty-ps,ps,color(Piece::T));

    const float ix=panel_x+202.0f;
    const float iy=panel_y+142.0f;
    drawTexturedCell(r,ix,iy,ps,color(Piece::I));
    drawTexturedCell(r,ix,iy+ps,ps,color(Piece::I));
    drawTexturedCell(r,ix,iy+ps*2,ps,color(Piece::I));
    drawTexturedCell(r,ix,iy+ps*3,ps,color(Piece::I));
    txt(r,panel_x+36,panel_y+382,textureName(cfg.texture),{155,205,220,255});
    txt(r,panel_x+36,panel_y+402,cfg.palette_affects_pieces?"PIECE PALETTE: ON":"PIECE PALETTE: OFF",
        cfg.palette_affects_pieces?C{125,220,170,255}:C{235,180,125,255});

    const char* desc="";
    switch(cfg.texture){
        case VisualTexture::Default:desc="Original FasTris blocks with the classic top highlight.";break;
        case VisualTexture::Flat:desc="Fastest minimalist solid-cell rendering.";break;
        case VisualTexture::Beveled:desc="Bevel + raised face with depth, border and softness controls.";break;
        case VisualTexture::SoftBevel:desc="Soft layered bevel with a pillow-like center sheen.";break;
        case VisualTexture::Glass:desc="Layered translucent glass with reflections and lit edges.";break;
        case VisualTexture::Neon:desc="Dark interior with bright configurable glowing-style edges.";break;
        case VisualTexture::Metallic:desc="Brushed metal with bands, streaks and rim shading.";break;
        case VisualTexture::Pixel:desc="Bounded mosaic pixels with bright and dark micro-cells.";break;
        case VisualTexture::Dots:desc="Batched dot pattern with bounded size and spacing.";break;
        case VisualTexture::Stripes:desc="Directional stripe bands with a subtle trailing shadow.";break;
        case VisualTexture::Grid:desc="Inset panel grid with dark gutters and edge highlights.";break;
        case VisualTexture::Wireframe:desc="Wireframe / hollow shell with adjustable center opacity.";break;
        case VisualTexture::Outline:desc="Filled dark center plus a strong piece-colored outline.";break;
        case VisualTexture::Recessed:desc="Inset/debossed tile with reversed edge lighting.";break;
        case VisualTexture::Arcade:desc="Chunky cabinet-style bevel with bright specular accent.";break;
        case VisualTexture::RetroLCD:desc="Segmented LCD-like dot/cell structure inside each tetromino cell.";break;
        default:break;
    }
    txt(r,80,585,desc,{155,168,185,255});
    txt(r,80,630,"UP/DOWN select   LEFT/RIGHT change   ENTER cycle/open   ESC back",{135,145,160,255});
}

void renderShaderSettings(SDL_Renderer*r,const AppConfig&cfg,int sel){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);
    txt(r,342,26,"SHADERS",{235,240,248,255},true);
    txt(r,230,66,"Controls change with the selected shader and apply immediately.",{145,155,170,255});

    const auto controls=shaderControls(cfg.shader);
    const int back_index=static_cast<int>(controls.size())+1;
    const int item_count=back_index+1;
    const float first_y=110.0f;
    const float available_h=430.0f;
    const float step=std::clamp(available_h/std::max(1,item_count),42.0f,58.0f);
    const float frame_h=std::min(46.0f,step-4.0f);

    auto drawRow=[&](int index,const std::string&label,const std::string&value,bool accent=false){
        const float y=first_y+step*index;
        const bool selected=index==sel;
        if(selected)fill(r,205,y-8,550,frame_h,{20,27,37,255});
        outline(r,205,y-8,550,frame_h,selected?C{120,220,255,255}:C{45,55,70,255});
        const C label_color=selected?C{120,220,255,255}:accent?C{245,180,110,255}:C{210,215,225,255};
        txt(r,226,y+5,(selected?"> ":"  ")+label,label_color,true);
        if(!value.empty())txt(r,565,y+8,value,selected?C{150,230,255,255}:C{155,205,220,255});
    };

    drawRow(0,"SHADER",shaderName(cfg.shader));
    for(std::size_t i=0;i<controls.size();++i){
        drawRow(static_cast<int>(i)+1,shaderControlName(controls[i]),shaderControlValueText(cfg,controls[i]));
    }
    drawRow(back_index,"BACK","",true);

    const char* desc1="";
    const char* desc2="";
    switch(cfg.shader){
        case VisualShader::None: desc1="Clean default output with no post-processing.";desc2="Choose another shader to reveal its own controls.";break;
        case VisualShader::CRT: desc1="CRT exposes scanlines, spacing, glow, true image curvature, vignette and softness.";desc2="Curvature warps the rendered frame itself instead of painting fake dark corners.";break;
        case VisualShader::Terminal: desc1="Terminal combines the old Terminal and Phosphor effects.";desc2="Glow, trails, scanlines and flicker now share one shader.";break;
        case VisualShader::LCD: desc1="LCD combines the old LCD and Pixel Grid effects.";desc2="Set Subpixel or Softness to 0 to disable either effect.";break;
        case VisualShader::DotMatrix: desc1="Dot Matrix exposes dot size, spacing and dot brightness.";desc2="The pattern can range from fine texture to chunky matrix cells.";break;
        case VisualShader::Bloom: desc1="Bloom exposes radius, threshold and softness.";desc2="Higher softness spreads the glow; threshold controls how restrained it feels.";break;
        case VisualShader::Scanlines: desc1="Scanlines exposes spacing and line thickness.";desc2="A lightweight display effect with no extra CRT treatment.";break;
        case VisualShader::Vignette: desc1="Vignette exposes radius and softness.";desc2="Lower radius makes the darkened edge region reach further inward.";break;
        case VisualShader::Analog: desc1="Analog exposes noise, flicker, horizontal jitter and distortion.";desc2="Keep values low for subtle instability or push them for a damaged signal look.";break;
        case VisualShader::Chromatic: desc1="Chromatic exposes RGB offset and direction.";desc2="Direction cycles horizontal, vertical and two diagonal variants.";break;
        case VisualShader::Ghosting: desc1="Ghosting exposes persistence and trail length.";desc2="Higher values make the display afterimage treatment more obvious.";break;
        case VisualShader::Arcade: desc1="Arcade exposes its bloom, scanlines, vignette and pixel-grid mix.";desc2="Tune the four components independently while Strength controls the whole effect.";break;
        default:break;
    }

    txt(r,205,565,desc1,{155,168,185,255});
    txt(r,205,590,desc2,{155,168,185,255});
    txt(r,205,628,"UP/DOWN select   LEFT/RIGHT change   ENTER cycle/open   ESC back",{135,145,160,255});
    txt(r,205,652,"All shader settings are presentation-only and saved in fastris.cfg.",{125,175,190,255});
}

void renderHelp(SDL_Renderer*r){beginCanvas(r,true);set(r,{11,14,20,255});SDL_RenderClear(r);txt(r,360,35,"HELP",{235,240,248,255},true);std::array<const char*,17> l={"Gameplay uses SDL event nanosecond timestamps.","OS key repeat is ignored; DAS/ARR are engine-driven.","Same seed + rules + input events = same simulation.","","Default keys:","Arrows: left/right/down    Space: hard drop","Up: rotate CW             Z: rotate CCW","A: rotate 180             C: hold","P: pause                  F5: restart","F6: save replay to file   ESC: menu","","Replay viewer:","Space pause, Left/Right seek 1 second","1/2/4/8 speed, N next piece, F6 save copy","","Seed and control rebinding are available from Settings.","Command line: --seed N --mode sprint --replay file"};for(int n=0;n<(int)l.size();++n)txt(r,160,105+n*30,l[n],{190,198,210,255},n<3);txt(r,160,660,"ESC back",{130,140,155,255});}

} // namespace fasttris::app
