#include "engine.hpp"
#include <cstdlib>
#include <cctype>
#include <functional>

namespace rf {
namespace {
struct Parser {
 const std::string& s;size_t pos=0;int depth=0;Expression out;std::string error;
 explicit Parser(const std::string& text):s(text){}
 void space(){while(pos<s.size()&&std::isspace((unsigned char)s[pos]))++pos;}
 bool take(char c){space();if(pos<s.size()&&s[pos]==c){++pos;return true;}return false;}
 bool emit(Op op,float value=0,int variable=0){
  if(out.count>=MaxCode){error="Expression exceeds 128 instructions";return false;}
  out.code[out.count++]={op,value,variable};return true;
 }
 bool primary(){
  if(++depth>24){error="Nesting exceeds 24";--depth;return false;}
  bool ok=atom();--depth;return ok;
 }
 bool atom(){
  space();if(take('(')){if(!sum()||!take(')')){error="Expected closing parenthesis";return false;}return true;}
  if(pos>=s.size()){error="Expected value";return false;}
  if(std::isdigit((unsigned char)s[pos])||s[pos]=='.'){
   char* end=nullptr;float v=std::strtof(s.c_str()+pos,&end);
   if(end==s.c_str()+pos||!std::isfinite(v)){error="Invalid number";return false;}
   pos=size_t(end-s.c_str());return emit(Op::Constant,v);
  }
  size_t begin=pos;while(pos<s.size()&&std::isalpha((unsigned char)s[pos]))++pos;
  std::string name=s.substr(begin,pos-begin);
  const char* vars[]={"x","y","z","cx","cy","cz"};
  for(int i=0;i<6;++i)if(name==vars[i])return emit(Op::Variable,0,i);
  if(name=="pi")return emit(Op::Constant,3.14159265359f);
  Op op;int arity=1;
  if(name=="sin")op=Op::Sin;else if(name=="cos")op=Op::Cos;
  else if(name=="abs")op=Op::Abs;else if(name=="sqrt")op=Op::Sqrt;
  else if(name=="log")op=Op::Log;else if(name=="exp")op=Op::Exp;
  else if(name=="min"){op=Op::Min;arity=2;}else if(name=="max"){op=Op::Max;arity=2;}
  else if(name=="pow"){op=Op::Pow;arity=2;}
  else{error="Unknown variable or function: "+name;return false;}
  if(!take('(')||!sum()||(arity==2&&(!take(',')||!sum()))||!take(')')){
   if(error.empty())error="Invalid function arguments";
   return false;
  }
  return emit(op);
 }
 bool unary(){if(take('-'))return unaryBounded()&&emit(Op::Neg);if(take('+'))return unaryBounded();return power();}
 bool unaryBounded(){if(++depth>24){error="Nesting exceeds 24";--depth;return false;}bool ok=unary();--depth;return ok;}
 bool power(){if(!primary())return false;if(take('^'))return unaryBounded()&&emit(Op::Pow);return true;}
 bool product(){if(!unary())return false;for(;;){if(take('*')){if(!unary()||!emit(Op::Mul))return false;}else if(take('/')){if(!unary()||!emit(Op::Div))return false;}else return true;}}
 bool sum(){if(!product())return false;for(;;){if(take('+')){if(!product()||!emit(Op::Add))return false;}else if(take('-')){if(!product()||!emit(Op::Sub))return false;}else return true;}}
};
}
bool Expression::compile(const std::string& text,std::string& error){
 if(text.empty()||text.size()>256){error="Use 1 to 256 characters";return false;}
 Parser p{text};if(!p.sum()){error=p.error;return false;}p.space();
 if(p.pos!=text.size()){error="Unexpected character at "+std::to_string(p.pos+1);return false;}
 int n=0,max=0;
 for(int i=0;i<p.out.count;++i){Op op=p.out.code[i].op;
  if(op==Op::Constant||op==Op::Variable)++n;
  else if(op==Op::Add||op==Op::Sub||op==Op::Mul||op==Op::Div||op==Op::Pow||op==Op::Min||op==Op::Max)--n;
  if(n<1){error="Invalid stack";return false;}if(n>max)max=n;
 }
 if(n!=1||max>32){error="Expression stack exceeds 32";return false;}
 p.out.stack=max;*this=p.out;error.clear();return true;
}
bool Expression::evaluate(Vec z,Vec c,Dual& result)const{
 if(count<=0)return false;
 Dual stackValues[32];int n=0;float vars[]={z.x,z.y,z.z,c.x,c.y,c.z};
 for(int i=0;i<count;++i){const Instruction& ins=code[i];Op op=ins.op;
  if(op==Op::Constant){stackValues[n++]={ins.value,{}};continue;}
  if(op==Op::Variable){Dual v;v.value=vars[ins.variable];v.d[ins.variable]=1;stackValues[n++]=v;continue;}
  bool binary=op==Op::Add||op==Op::Sub||op==Op::Mul||op==Op::Div||op==Op::Pow||op==Op::Min||op==Op::Max;
  Dual b;if(binary)b=stackValues[--n];Dual& a=stackValues[n-1];float av=a.value,bv=b.value,da=0,db=0;
  switch(op){
   case Op::Add:a.value=av+bv;da=1;db=1;break;
   case Op::Sub:a.value=av-bv;da=1;db=-1;break;
   case Op::Mul:a.value=av*bv;da=bv;db=av;break;
   case Op::Div:if(std::fabs(bv)<1e-12f)return false;a.value=av/bv;da=1/bv;db=-av/(bv*bv);break;
   case Op::Pow:{
    bool varying=false;for(float d:b.d)varying|=d!=0;
    if(av<=0&&(varying||std::floor(bv)!=bv))return false;
    if(av==0&&bv<1)return false;
    a.value=std::pow(av,bv);da=bv==0?0:bv*std::pow(av,bv-1);db=av>0?a.value*std::log(av):0;break;
   }
   case Op::Min:case Op::Max:{bool useA=op==Op::Min?av<=bv:av>=bv;a.value=useA?av:bv;da=useA?1:0;db=useA?0:1;break;}
   case Op::Neg:a.value=-av;da=-1;break;
   case Op::Sin:a.value=std::sin(av);da=std::cos(av);break;
   case Op::Cos:a.value=std::cos(av);da=-std::sin(av);break;
   case Op::Abs:a.value=std::fabs(av);da=av<0?-1:1;break;
   case Op::Sqrt:if(av<0)return false;a.value=std::sqrt(av);da=.5f/std::fmax(a.value,1e-12f);break;
   case Op::Log:if(av<=0)return false;a.value=std::log(av);da=1/av;break;
   case Op::Exp:a.value=std::exp(av);da=a.value;break;
   default:return false;
  }
  if(!std::isfinite(a.value))return false;
  for(int j=0;j<6;++j){a.d[j]=da*a.d[j]+db*b.d[j];if(!std::isfinite(a.d[j]))return false;}
 }
 result=stackValues[0];return true;
}
}
