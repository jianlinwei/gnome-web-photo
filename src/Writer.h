/*
 *  Copyright © 2005 Christian Persch
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2.1, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#ifndef WRITER_H
#define WRITER_H

#include <nsStringGlue.h>
#include <gfxImageSurface.h>
#include <gtkmozembed.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include <png.h>

#ifdef ENABLE_JPEG
#include <jpeglib.h>
#include <jerror.h>
#endif

class Writer
{
public:
  Writer(GtkMozEmbed*, const char *);
  virtual ~Writer() { };

  PRBool Write ();

  virtual PRBool Prepare(gfxImageSurface *) = 0;
  virtual PRBool Finish() = 0;
  virtual void WriteSurface(gfxImageSurface*, PRUint32, PRUint32) = 0;

protected:
  GtkMozEmbed *mEmbed;
  nsCAutoString mFilename;
  nsCAutoString mTitle;
  nsCAutoString mSpec;
  PRUint32 mWidth;
  PRUint32 mHeight;

public:
  PRBool mInitialised;
  PRBool mHadError;
};

class PNGWriter : public Writer
{
public:
  PNGWriter (GtkMozEmbed*, const char *);
  virtual ~PNGWriter();

  virtual PRBool Prepare(gfxImageSurface *);
  virtual PRBool Finish();

  virtual void WriteSurface(gfxImageSurface*, PRUint32, PRUint32);

  static void WarnCallback(png_structp, png_const_charp);
  static void ErrorCallback(png_structp, png_const_charp);

private:
  FILE *mFile;
  png_structp mPNG;
  png_infop mInfo;
  png_textp mText;
  png_bytep mRow;
};

class PPMWriter : public Writer
{
public:
  PPMWriter (GtkMozEmbed*, const char *);
  virtual ~PPMWriter();

  virtual PRBool Prepare(gfxImageSurface *);
  virtual PRBool Finish();

  virtual void WriteSurface(gfxImageSurface*, PRUint32, PRUint32);

private:
  FILE *mFile;
  unsigned char* mRow;
};

class ThumbnailWriter : public Writer
{
public:
  ThumbnailWriter(GtkMozEmbed*, const char*, PRUint32);
  virtual ~ThumbnailWriter();

  virtual PRBool Prepare(gfxImageSurface *);
  virtual PRBool Finish();
  virtual void WriteSurface(gfxImageSurface*, PRUint32, PRUint32);

private:
  GdkPixdata *mData;
  PRUint32 mSize;
  PRUint8 *mDest;
};

#ifdef ENABLE_JPEG
class JPEGWriter : public Writer
{
public:
  JPEGWriter (GtkMozEmbed*, const char *, PRUint16);
  virtual ~JPEGWriter();

  virtual PRBool Prepare(gfxImageSurface *);
  virtual PRBool Finish();

  virtual void WriteSurface(gfxImageSurface*, PRUint32, PRUint32);

private:
  PRUint16 mQuality;
  FILE *mFile;
  struct jpeg_compress_struct mJPEG;
  struct jpeg_error_mgr mHandler;
  PRUint8* mRow;
};
#endif /* ENABLE_JPEG */

#endif
