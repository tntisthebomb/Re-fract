#pragma once
#include "engine.hpp"
namespace rf {
// Both screens use a logical landscape canvas, independent of 3DS memory layout.
class Canvas {
 public:
 int width,height;std::vector<Color> pixels;
 std::vector<Color> texture;
 Canvas(int w,int h):width(w),height(h),pixels(w*h){}
 void point(int x,int y,Color c);
 void rect(int x,int y,int w,int h,Color c);
 void text(int x,int y,const std::string& s,Color c,int scale=1);
 void grunge();
};
Vec hsvColor(float hue,float saturation,float value);
Vec colorHSV(Vec color);
void drawColorPicker(Canvas& canvas,float hue,float saturation,float value);
struct Row {std::string label,value;};
void drawPanel(Canvas& c,int tab,int selected,const std::vector<Row>& rows,const std::string& status,
 const std::string& name,int block,float progress,bool quality,float workMs);
}
