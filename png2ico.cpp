/*
TODO:
>When feeding the image ms26.png (see message "what's wrong with this
>image") to png2ico I get the message
>
>libpng error: Read error
>
>How do I suppress the outputting of this message? I was surprised that
>libpng outputs a message in addition to jumping to the setjmp error
>handler (which outputs its own message). Do I need to write my own error
>handling functions or is there a simpler way (of course I don't want to
>recompile libpng itself)?

That message is the message printed by the setjmp error handler.

You can replace the error handler without recompiling libpng.  Just
include a pointer to your error handler in png_read_create_struct().
See for example pngtest.c which illustrates replacing the error handler.
*/

/* Copyright (C) 2002 Matthias S. Benkmann <matthias@winterdrache.de>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License (ONLY THIS VERSION).

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

/*
Notes about transparent and inverted pixels:
 Handling of transparent pixels is inconsistent in Windows. Sometimes a
 pixel with an AND mask value of 1 is just transparent (i.e. its color 
 value is ignored), sometimes the color value is XORed with the background to
 give some kind of inverted effect. A closer look at bmp.txt suggests that
 the latter behaviour is the correct one but because it often doesn't happen
 it's de facto undefined behaviour.
 Furthermore, sometimes the AND mask entry seems to be interpreted as a
 color index, i.e. a value of 1 will AND the background with color 1.
 Conclusion: The most robust solution seems to be:
               -color 0 always 0,0,0
               -color 1 always 255,255,255
               -all transparent pixels get color 0
*/


#include <cstdio>
#include <vector>
#include <climits>

#if __GNUC__ > 2
#include <ext/hash_map>
#else
#include <hash_map>
#endif

#include <png.h>

#include "VERSION"

using namespace std;
namespace __gnu_cxx{};
using namespace __gnu_cxx;

const int word_max=65535;
const int transparency_threshold=196;
const int color_reduce_warning_threshold=512; //maximum quadratic euclidean distance in RGB color space that a palette color may have to a source color assigned to it before a warning is issued
const unsigned int slow_reduction_warn_threshold=262144; //number of colors in source image times number of colors in target image that triggers the warning that the reduction may take a while

void writeWord(FILE* f, int word)
{
  char data[2];
  data[0]=(word&255);
  data[1]=(word>>8)&255;
  if (fwrite(data,2,1,f)!=1) {perror("Write error"); exit(1);};
};

void writeDWord(FILE* f, unsigned int dword)
{
  char data[4];
  data[0]=(dword&255);
  data[1]=(dword>>8)&255;
  data[2]=(dword>>16)&255;
  data[3]=(dword>>24)&255;
  if (fwrite(data,4,1,f)!=1) {perror("Write error"); exit(1);};
};

void writeByte(FILE* f, int byte)
{
  char data[1];
  data[0]=(byte&255);
  if (fwrite(data,1,1,f)!=1) {perror("Write error"); exit(1);};
};



struct png_data
{
  png_structp png_ptr;
  png_infop info_ptr;
  png_infop end_info;
  png_uint_32 width, height;
  png_colorp palette;
  png_bytepp transMap;
  int num_palette;
  int requested_colors;
  int col_bits;
  png_data():png_ptr(NULL),info_ptr(NULL),end_info(NULL),width(0),height(0),
             palette(NULL),transMap(NULL),num_palette(0),requested_colors(0),col_bits(0){};
};

int andMaskLineLen(const png_data& img)
{
  int len=(img.width+7)>>3;
  return (len+3)&~3;
};

int xorMaskLineLen(const png_data& img)
{
  int pixelsPerByte=(8/img.col_bits);
  return ((img.width+pixelsPerByte-1)/pixelsPerByte+3)&~3;
};

typedef bool (*checkTransparent_t)(png_bytep, png_data&);

bool checkTransparent1(png_bytep data, png_data&)
{
  return (data[3]<transparency_threshold);
};

bool checkTransparent3(png_bytep, png_data&)
{
  return false;
};

//returns true if color reduction resulted in at least one of the image's colors 
//being mapped to a palette color with a quadratic distance of more than
//color_reduce_warning_threshold
bool convertToIndexed(png_data& img, bool hasAlpha)
{
  int maxColors=img.requested_colors;
  
  size_t palSize=sizeof(png_color)*256; //must reserve space for 256 entries here because write loop in main() expects it
  img.palette=(png_colorp)malloc(palSize);
  memset(img.palette,0,palSize); //must initialize whole palette because write loop in main() expects it
  img.num_palette=0;
  
  checkTransparent_t checkTrans=checkTransparent1;
  int bytesPerPixel=4;
  if (!hasAlpha)
  {
    bytesPerPixel=3;
    checkTrans=checkTransparent3;  
  };
  
  //first pass: gather all colors, make sure
  //alpha channel (if present) contains only 0 and 255
  //if an alpha channel is present, set all transparent pixels to RGBA (0,0,0,0)
  //transparent pixels will already be mapped to palette entry 0, non-transparent
  //pixels will not get a mapping yet (-1)
  hash_map<unsigned int,signed int> mapQuadToPalEntry;
  png_bytep* row_pointers=png_get_rows(img.png_ptr, img.info_ptr);
  
  for (int y=img.height-1; y>=0; --y)
  {
    png_bytep pixel=row_pointers[y];
    for (unsigned i=0; i<img.width; ++i)
    {
      unsigned int quad=pixel[0]+(pixel[1]<<8)+(pixel[2]<<16);
      bool trans=(*checkTrans)(pixel,img);
     
      if (hasAlpha) 
      {
        if (trans)
        {
          pixel[0]=0;
          pixel[1]=0;
          pixel[2]=0;
          pixel[3]=0;
          quad=0;
        }
        else pixel[3]=255;
        
        quad+=(pixel[3]<<24);
      }
      else if (!trans) quad+=(255<<24);
      
      if (trans) 
        mapQuadToPalEntry[quad]=0;
      else  
        mapQuadToPalEntry[quad]=-1;
    
      pixel+=bytesPerPixel;
    };  
  };
  
  //always allocate entry 0 to black and entry 1 to white because
  //sometimes AND mask is interpreted as color index
  img.num_palette=2;
  img.palette[0].red=0;
  img.palette[0].green=0;
  img.palette[0].blue=0;
  img.palette[1].red=255;
  img.palette[1].green=255;
  img.palette[1].blue=255;
  
  mapQuadToPalEntry[255<<24]=0; //map (non-transparent) black to entry 0
  mapQuadToPalEntry[255+(255<<8)+(255<<16)+(255<<24)]=1; //map (non-transparent) white to entry 1
  
  if (mapQuadToPalEntry.size()*img.requested_colors>slow_reduction_warn_threshold)
  {
    fprintf(stdout,"Please be patient. My color reduction algorithm is really slow.\n");
  };
  
  //Now fill up the palette with colors from the image by repeatedly picking the
  //color most different from the previously picked colors and adding this to the
  //palette. This is done to make sure that in case there are more image colors than
  //palette entries, palette entries are not wasted on similar colors.
  while(img.num_palette<maxColors)
  {
    unsigned int mostDifferentQuad=0;
    int mdqMinDist=-1; //smallest distance to an entry in the palette for mostDifferentQuad
    int mdqDistSum=-1; //sum over all distances to palette entries for mostDifferentQuad
    hash_map<unsigned int,signed int>::iterator stop=mapQuadToPalEntry.end();
    hash_map<unsigned int,signed int>::iterator iter=mapQuadToPalEntry.begin();
    while(iter!=stop)
    {
      hash_map<unsigned int,signed int>::value_type& mapping=*iter++;
      if (mapping.second<0)
      {
        unsigned int quad=mapping.first;
        int red=quad&255;  //must be signed
        int green=(quad>>8)&255;
        int blue=(quad>>16)&255;
        int distSum=0;
        int minDist=INT_MAX;
        for (int i=0; i<img.num_palette; ++i)
        {
          int dist=(red-img.palette[i].red);
          dist*=dist;
          int temp=(green-img.palette[i].green);
          dist+=temp*temp;
          temp=(blue-img.palette[i].blue);
          dist+=temp*temp;
          if (dist<minDist) minDist=dist;
          distSum+=dist;
        };  
        
        if (minDist>mdqMinDist || (minDist==mdqMinDist && distSum>mdqDistSum))
        {
          mostDifferentQuad=quad;
          mdqMinDist=minDist;
          mdqDistSum=distSum;
        };
      };
    };
    
    if (mdqMinDist>0) //if we have found a most different quad, add it to the palette
    {                  //and map it to the new palette entry
      int palentry=img.num_palette;
      img.palette[palentry].red=mostDifferentQuad&255;
      img.palette[palentry].green=(mostDifferentQuad>>8)&255;
      img.palette[palentry].blue=(mostDifferentQuad>>16)&255;
      mapQuadToPalEntry[mostDifferentQuad]=palentry;
      ++img.num_palette;
    }
    else break; //otherwise (i.e. all quads are mapped) the palette is finished
  };

  //Now map all yet unmapped colors to the most appropriate palette entry
  hash_map<unsigned int,signed int>::iterator stop=mapQuadToPalEntry.end();
  hash_map<unsigned int,signed int>::iterator iter=mapQuadToPalEntry.begin();
  while(iter!=stop)
  {
    hash_map<unsigned int,signed int>::value_type& mapping=*iter++;
    if (mapping.second<0)
    {
      unsigned int quad=mapping.first;
      int red=quad&255;  //must be signed
      int green=(quad>>8)&255;
      int blue=(quad>>16)&255;
      int minDist=INT_MAX;
      int bestIndex=0;
      for (int i=0; i<img.num_palette; ++i)
      {
        int dist=(red-img.palette[i].red);
        dist*=dist;
        int temp=(green-img.palette[i].green);
        dist+=temp*temp;
        temp=(blue-img.palette[i].blue);
        dist+=temp*temp;
        if (dist<minDist) { minDist=dist; bestIndex=i; };
      };
      
      mapping.second=bestIndex; 
    };
  };
  
  //Adjust all palette entries (except for 0 and 1) to be the mean of all
  //colors mapped to it
  for (int i=2; i<img.num_palette; ++i)
  {
    int red=0;
    int green=0;
    int blue=0;
    int numMappings=0;
    hash_map<unsigned int,signed int>::iterator stop=mapQuadToPalEntry.end();
    hash_map<unsigned int,signed int>::iterator iter=mapQuadToPalEntry.begin();
    while(iter!=stop)
    {
      hash_map<unsigned int,signed int>::value_type& mapping=*iter++;
      if (mapping.second==i)
      {
        unsigned int quad=mapping.first;
        red+=quad&255;
        green+=(quad>>8)&255;
        blue+=(quad>>16)&255;
        ++numMappings;
      };
    };
    
    if (numMappings>0)
    {
      img.palette[i].red=(red+red+numMappings)/(numMappings+numMappings);
      img.palette[i].green=(green+green+numMappings)/(numMappings+numMappings);
      img.palette[i].blue=(blue+blue+numMappings)/(numMappings+numMappings);
    };
  };

  //Now determine if a non-transparent source color got mapped to a target color that 
  //has a distance that exceeds the threshold
  bool tooManyColors=false;
  stop=mapQuadToPalEntry.end();
  iter=mapQuadToPalEntry.begin();
  while(iter!=stop)
  {
    hash_map<unsigned int,signed int>::value_type& mapping=*iter++;
    unsigned int quad=mapping.first;
    if ((quad>>24)!=0) //if color is not transparent
    {
      int red=quad&255;
      int green=(quad>>8)&255;
      int blue=(quad>>16)&255;
      int i=mapping.second;
      int dist=(red-img.palette[i].red);
      dist*=dist;
      int temp=(green-img.palette[i].green);
      dist+=temp*temp;
      temp=(blue-img.palette[i].blue);
      dist+=temp*temp;
      if (dist>color_reduce_warning_threshold) tooManyColors=true;
    };
  };
  

  int transLineLen=andMaskLineLen(img);
  int transLinePad=transLineLen - ((img.width+7)/8);
  img.transMap=(png_bytepp)malloc(img.height*sizeof(png_bytep));
  
  //second pass: convert RGB to palette entries
  for (int y=img.height-1; y>=0; --y)
  {
    png_bytep row=row_pointers[y];
    png_bytep pixel=row;
    int count8=0;
    int transbyte=0;
    png_bytep transPtr=img.transMap[y]=(png_bytep)malloc(transLineLen);
    
    for (unsigned i=0; i<img.width; ++i)
    {
      bool trans=((*checkTrans)(pixel,img));
      unsigned int quad=pixel[0]+(pixel[1]<<8)+(pixel[2]<<16);
      if (!trans) quad+=(255<<24); //NOTE: alpha channel has already been set to 255 for non-transparent pixels, so this is correct even for images with alpha channel
      
      if (trans) ++transbyte; 
      if (++count8==8)
      {
        *transPtr++ = transbyte;
        count8=0;
        transbyte=0;
      };
      transbyte+=transbyte; //shift left 1
      
      int palentry=mapQuadToPalEntry[quad];
      row[i]=palentry;
      pixel+=bytesPerPixel;
    };

    for(int i=0; i<transLinePad; ++i) *transPtr++ = 0;
  };
  
  return tooManyColors;
};

//packs a line of width pixels (1 byte per pixel) in row, with 8/nbits pixels packed
//into each byte
//returns the new number of bytes in row
int pack(png_bytep row,int width,int nbits)
{
  int pixelsPerByte=8/nbits;
  if (pixelsPerByte<=1) return width;
  int ander=(1<<nbits)-1;
  int outByte=0;
  int count=0;
  int outIndex=0;
  for (int i=0; i<width; ++i)
  {
    outByte+=(row[i]&ander);
    if (++count==pixelsPerByte) 
    {
      row[outIndex]=outByte;
      count=0;
      ++outIndex;
      outByte=0;
    };
    outByte<<=nbits;
  };

  if (count>0) 
  {
    outByte<<=nbits*(pixelsPerByte-count);
    row[outIndex]=outByte;
    ++outIndex;
  };  
  
  return outIndex;
};


void usage()
{
  fprintf(stderr,version"\n");
  fprintf(stderr,"USAGE: png2ico icofile [--colors <num>] pngfile1 [pngfile2 ...]\n");
  exit(1);
};

int main(int argc, char* argv[])
{
  if (argc<3) usage();
  
  if (argc-2 > word_max) 
  {
    fprintf(stderr,"Too many PNG files\n");
    exit(1);
  };
  
  vector<png_data> pngdata;
  
  static int numColors=256; //static to get rid of longjmp() clobber warning
  static const char* outfileName=NULL;
  
  //i is static because used in a setjmp() block
  for (static int i=1; i<argc; ++i)
  {
    if (strcmp(argv[i],"--colors")==0)
    {
      ++i;
      if (i>=argc)
      {
        fprintf(stderr,"Number missing after --colors\n");
        exit(1);
      };
      char* endptr;
      long num=strtol(argv[i],&endptr,10);
      if (*(argv[i])==0 || *endptr!=0 || (num!=2 && num!=16 && num!=256))
      {
        fprintf(stderr,"Illegal number of colors\n");
        exit(1);
      };
      numColors=num;
      continue;
    };
    
    if (outfileName==NULL) { outfileName=argv[i]; continue; };
    
    FILE* pngfile=fopen(argv[i],"rb");
    if (pngfile==NULL)  {perror(argv[i]); exit(1);};
    png_byte header[8];
    if (fread(header,8,1,pngfile)!=1) {perror(argv[i]); exit(1);};
    if (png_sig_cmp(header,0,8))
    {
      fprintf(stderr,"%s: Not a PNG file\n",argv[i]);
      exit(1);
    };
    
    png_data data;
    data.requested_colors=numColors;
    for (data.col_bits=1; (1<<data.col_bits)<numColors; ++data.col_bits);
    
    data.png_ptr=png_create_read_struct
                   (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!data.png_ptr)
    {
      fprintf(stderr,"png_create_read_struct error\n");
      exit(1);
    };  

    data.info_ptr=png_create_info_struct(data.png_ptr);
    if (!data.info_ptr)
    {
      png_destroy_read_struct(&data.png_ptr, (png_infopp)NULL, (png_infopp)NULL);
      fprintf(stderr,"png_create_info_struct error\n");
      exit(1);
    };

    data.end_info=png_create_info_struct(data.png_ptr);
    if (!data.end_info)
    {
      png_destroy_read_struct(&data.png_ptr, &data.info_ptr, (png_infopp)NULL);
      fprintf(stderr,"png_create_info_struct error\n");
      exit(1);
    };
    
    if (setjmp(png_jmpbuf(data.png_ptr)))
    {
      png_destroy_read_struct(&data.png_ptr, &data.info_ptr, &data.end_info);
      fprintf(stderr,"%s: PNG error\n",argv[i]);
      exit(1);
    };
    
    png_init_io(data.png_ptr, pngfile);
    png_set_sig_bytes(data.png_ptr,8);
    int trafo=PNG_TRANSFORM_PACKING|PNG_TRANSFORM_STRIP_16|PNG_TRANSFORM_EXPAND;
    png_read_png(data.png_ptr, data.info_ptr, trafo , NULL);

    int bit_depth, color_type, interlace_type, compression_type, filter_method;
    png_get_IHDR(data.png_ptr, data.info_ptr, &data.width, &data.height, &bit_depth, &color_type, 
                 &interlace_type, &compression_type, &filter_method);
                 
    
    
    if ( (data.width&7)!=0 || data.width>=256 || data.height>=256)
    {
      //I don't know if the following is really a requirement (bmp.txt says that
      //only 16x16, 32x32 and 64x64 are allowed but that doesn't seem right) but
      //if the width is not a multiple of 8, then the loop creating the and mask later
      //doesn't work properly because it doesn't shift in padding bits
      fprintf(stderr,"%s: Width must be multiple of 8 and <256. Height must be <256.\n",argv[i]);
      exit(1);
    };
    
    if ((color_type & PNG_COLOR_MASK_COLOR)==0)
    {
      fprintf(stderr,"%s: Grayscale image not supported\n",argv[i]);
      exit(1);
    };
    
    if (color_type==PNG_COLOR_TYPE_PALETTE)
    {
      fprintf(stderr,"This can't happen. PNG_TRANSFORM_EXPAND transforms image to RGB.\n");
      exit(1);
    }
    else
    {
      if (convertToIndexed(data, ((color_type & PNG_COLOR_MASK_ALPHA)!=0)))
      {
        fprintf(stderr,"%s: Warning! Color reduction may not be optimal!\nIf the result is not satisfactory, reduce the number of colors\nbefore using png2ico.\n",argv[i]);
      };
    };  
    
    pngdata.push_back(data);
    
    fclose(pngfile);
  };
  

  if (outfileName==NULL || pngdata.size()<1) usage();
  
  FILE* outfile=fopen(outfileName,"wb");
  if (outfile==NULL) {perror(argv[1]); exit(1);};
  
  writeWord(outfile,0); //idReserved
  writeWord(outfile,1); //idType
  writeWord(outfile,pngdata.size()); //idCount
  
  int offset=6+pngdata.size()*16;
  
  vector<png_data>::const_iterator img;
  for(img=pngdata.begin(); img!=pngdata.end(); ++img)
  {
    writeByte(outfile,img->width); //bWidth
    writeByte(outfile,img->height); //bHeight
    writeByte(outfile,img->requested_colors&255); //bColorCount
    writeByte(outfile,0); //bReserved
    writeWord(outfile,0); //wPlanes
    writeWord(outfile,0); //wBitCount
    int resSize=40+img->requested_colors*4+(andMaskLineLen(*img)+xorMaskLineLen(*img))*img->height;
    writeDWord(outfile,resSize); //dwBytesInRes
    writeDWord(outfile,offset); //dwImageOffset
    offset+=resSize;
  };
  
  
  for(img=pngdata.begin(); img!=pngdata.end(); ++img)
  {
    writeDWord(outfile,40); //biSize
    writeDWord(outfile,img->width); //biWidth
    writeDWord(outfile,2*img->height); //biHeight (2 times because the 2 masks are counted)
    writeWord(outfile,1);   //biPlanes
    writeWord(outfile,img->col_bits);   //biBitCount
    writeDWord(outfile,0);  //biCompression
    writeDWord(outfile,(andMaskLineLen(*img)+xorMaskLineLen(*img))*img->height);  //biSizeImage
    writeDWord(outfile,0);  //biXPelsPerMeter
    writeDWord(outfile,0);  //biYPelsPerMeter
    writeDWord(outfile,0); //biClrUsed (MUST BE 0 ACCORDING TO bmp.txt!!! I tried putting the real number here, but this breaks icons in some places)
    writeDWord(outfile,0);   //biClrImportant
    for (int i=0; i<img->requested_colors; ++i)
    {
      char col[4];
      col[0]=img->palette[i].blue;
      col[1]=img->palette[i].green;
      col[2]=img->palette[i].red;
      col[3]=0;
      if (fwrite(col,4,1,outfile)!=1) {perror("Write error"); exit(1);};
    };
    
    png_bytep* row_pointers=png_get_rows(img->png_ptr, img->info_ptr);
    for (int y=img->height-1; y>=0; --y)
    {
      png_bytep row=row_pointers[y];
      int newLength=pack(row,img->width,img->col_bits);
      if (fwrite(row,newLength,1,outfile)!=1) {perror("Write error"); exit(1);};
      for(int i=0; i<xorMaskLineLen(*img)-newLength; ++i) writeByte(outfile,0);
    };
    
    for (int y=img->height-1; y>=0; --y)
    {
      png_bytep transPtr=img->transMap[y];
      if (fwrite(transPtr,andMaskLineLen(*img),1,outfile)!=1) {perror("Write error"); exit(1);};
    };
  };
  
  fclose(outfile);
};


