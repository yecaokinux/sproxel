#include <QImage>
#include "SproxelProject.h"

static SproxelColor colorFromHSV(float h, float s, float v)
{
  if (s == 0) {
    return SproxelColor(v, v, v, 1);
  }

  float part     = h / 60.f;
  int   i        = floor(part);
  float fraction = part - (float)i;
  float c0       = v * (1 - s );
  float c1       = v * (1 - s * fraction);
  float c2       = v * (1 - s * (1-fraction));

  if (i == 0) {
    return SproxelColor(v, c2, c0, 1);
  } else if (i == 1) {
    return SproxelColor(c1, v, c0, 1);
  } else if (i == 2) {
    return SproxelColor(c0, v, c2, 1);
  } else if (i == 3) {
    return SproxelColor(c0, c1, v, 1);
  } else if (i == 4) {
    return SproxelColor(c2, c0, v, 1);
  } else {
    return SproxelColor(v, c0, c1, 1);
  }
}

SproxelProject::SproxelProject()
{
  mainPalette=new ColorPalette();
  mainPalette->resize(256);
  mainPalette->setName("main");
  for (int y=0; y<15; ++y)
  {
    float sat = 1.f - (y / 16.f)*0.5f;
    float value = 1.f - (y / 16.f);

    for (int x=0; x<16; ++x)
    {
      float hue = x * 22.5f;
      mainPalette->setColor(y*16+x, colorFromHSV(hue, sat, value));
    }
  }

  for (int x=0; x<16; ++x)
  {
    float value = x / 16.f;
    mainPalette->setColor(240+x, colorFromHSV(0, 0, value));
  }
  palettes.push_back(mainPalette);
}

VoxelGridLayerPtr VoxelGridLayer::fromQImage(QImage readMe, ColorPalettePtr pal)
{
  QString tempStr;
  tempStr = readMe.text("VoxelGridDimX");
  int sizeX = tempStr.toInt();
  tempStr = readMe.text("VoxelGridDimY");
  int sizeY = tempStr.toInt();
  tempStr = readMe.text("VoxelGridDimZ");
  int sizeZ = tempStr.toInt();

  if (sizeX == 0 || sizeY == 0 || sizeZ == 0) return VoxelGridLayerPtr();

  readMe = readMe.mirrored();

  VoxelGridLayerPtr layer(new VoxelGridLayer());

  bool indexed=false;
  if (readMe.colorCount()>0)
  {
    indexed=true;
    layer->setPalette(pal);
  }

  layer->resize(Imath::Box3i(Imath::V3i(0), Imath::V3i(sizeX, sizeY, sizeZ)-Imath::V3i(1)));

  for (int slice = 0; slice < sizeZ; slice++)
  {
    const int sliceOffset = slice * sizeX;
    for (int y = 0; y < sizeY; y++)
    {
      for (int x = 0; x < sizeX; x++)
      {
        int index=-1;
        if (indexed) index=readMe.pixelIndex(x+sliceOffset, y);
        QRgb pixelValue = readMe.pixel(x+sliceOffset, y);
        Imath::Color4f color(
          (float)qRed  (pixelValue) / 255.0f,
          (float)qGreen(pixelValue) / 255.0f,
          (float)qBlue (pixelValue) / 255.0f,
          (float)qAlpha(pixelValue) / 255.0f);
        layer->set(Imath::V3i(x, y, slice), color, index);
      }
    }
  }

  return layer;
}


QImage VoxelGridLayer::makeQImage() const
{
  const Imath::Box3i dim=bounds();
  const Imath::V3i cellDim = dim.size()+Imath::V3i(1);

  // TODO: Offer other options besides XY slices?  Directionality?  Ordering?
  const int height = cellDim.y;
  const int width = cellDim.x * cellDim.z;
  QImage writeMe(QSize(width, height), m_ind?QImage::Format_Indexed8:QImage::Format_ARGB32);

  if (m_ind)
  {
    if (m_palette)
    {
      writeMe.setColorCount(m_palette->numColors());
      for (int i=0; i<m_palette->numColors(); ++i)
      {
        SproxelColor c=m_palette->color(i)*255.0f;
        writeMe.setColor(i, qRgba(int(c.r), int(c.g), int(c.b), int(c.a)));
      }
    }

    for (int slice = 0; slice < cellDim.z; slice++)
    {
      const int sliceOffset = slice * cellDim.x;
      for (int y = 0; y < cellDim.y; y++)
      {
        for (int x = 0; x < cellDim.x; x++)
          writeMe.setPixel(x+sliceOffset, y, getInd(Imath::V3i(x, y, slice)+dim.min));
      }
    }
  }
  else
  {
    for (int slice = 0; slice < cellDim.z; slice++)
    {
      const int sliceOffset = slice * cellDim.x;
      for (int y = 0; y < cellDim.y; y++)
      {
        for (int x = 0; x < cellDim.x; x++)
        {
          const Imath::Color4f colorScaled = getColor(Imath::V3i(x, y, slice)+dim.min) * 255.0f;
          writeMe.setPixel(x+sliceOffset, y, qRgba(
            (int)colorScaled.r,
            (int)colorScaled.g,
            (int)colorScaled.b,
            (int)colorScaled.a));
        }
      }
    }
  }

  writeMe = writeMe.mirrored();   // QT Bug: mirrored() doesn't preserve text.

  QString tempStr;
  writeMe.setText("SproxelFileVersion", "1");
  writeMe.setText("VoxelGridDimX", tempStr.setNum(cellDim.x));
  writeMe.setText("VoxelGridDimY", tempStr.setNum(cellDim.y));
  writeMe.setText("VoxelGridDimZ", tempStr.setNum(cellDim.z));

  return writeMe;
}


//ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ//


VoxelGridLayerPtr VoxelGridGroup::bakeLayers() const
{
  VoxelGridLayerPtr grid(new VoxelGridLayer());

  ColorPalettePtr palette;
  bool hasRgb=false, hasMultiPal=false;

  foreach (VoxelGridLayerPtr layer, m_layers)
    if (layer->palette())
    {
      if (!palette) palette=layer->palette();
      else if (layer->palette()!=palette) hasMultiPal=true;
    }
    else
      hasRgb=true;

  if (hasRgb || hasMultiPal) palette=NULL;

  grid->setPalette(palette);

  Imath::Box3i ext=bounds();
  grid->resize(ext);

  for (int z=ext.min.z; z<=ext.max.z; ++z)
    for (int y=ext.min.y; y<=ext.max.y; ++y)
      for (int x=ext.min.x; x<=ext.max.x; ++x)
      {
        Imath::V3i at(x, y, z);
        grid->set(at, get(at), getInd(at));
      }

  return grid;
}
