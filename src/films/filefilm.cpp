
/*
    pbrt source code is Copyright(c) 1998-2016
                        Matt Pharr, Greg Humphreys, and Wenzel Jakob.

    This file is part of pbrt.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */


// core/film.cpp*
#include "filefilm.h"
#include "paramset.h"
#include "imageio.h"
#include "stats.h"

namespace pbrt {


FileFilm::FileFilm(const Point2i &resolution, const Bounds2f &cropWindow,
         std::unique_ptr<Filter> filter, Float diagonal,
         const std::string &filename, Float scale,
                   Float maxSampleLuminance)
    : Film(resolution, cropWindow, std::move(filter), diagonal, filename, scale,
           maxSampleLuminance) {}

 void FileFilm::WriteImage(Float splatScale) {
    // Convert image to RGB and compute final pixel values
    LOG(INFO) <<
        "Converting image to RGB and computing final weighted pixel values";
    std::unique_ptr<Float[]> rgb(new Float[3 * croppedPixelBounds.Area()]);
    int offset = 0;
    for (Point2i p : croppedPixelBounds) {
        // Convert pixel XYZ color to RGB
        Pixel &pixel = GetPixel(p);
        XYZToRGB(pixel.xyz, &rgb[3 * offset]);

        // Normalize pixel with weight sum
        Float filterWeightSum = pixel.filterWeightSum;
        if (filterWeightSum != 0) {
            Float invWt = (Float)1 / filterWeightSum;
            rgb[3 * offset] = std::max((Float)0, rgb[3 * offset] * invWt);
            rgb[3 * offset + 1] =
                std::max((Float)0, rgb[3 * offset + 1] * invWt);
            rgb[3 * offset + 2] =
                std::max((Float)0, rgb[3 * offset + 2] * invWt);
        }

        // Add splat value at pixel
        Float splatRGB[3];
        Float splatXYZ[3] = {pixel.splatXYZ[0], pixel.splatXYZ[1],
                             pixel.splatXYZ[2]};
        XYZToRGB(splatXYZ, splatRGB);
        rgb[3 * offset] += splatScale * splatRGB[0];
        rgb[3 * offset + 1] += splatScale * splatRGB[1];
        rgb[3 * offset + 2] += splatScale * splatRGB[2];

        // Scale pixel value by _scale_
        rgb[3 * offset] *= scale;
        rgb[3 * offset + 1] *= scale;
        rgb[3 * offset + 2] *= scale;
        ++offset;
    }

    // Write RGB image
    LOG(INFO) << "Writing image " << filename << " with bounds " <<
        croppedPixelBounds;
    pbrt::WriteImage(filename, &rgb[0], croppedPixelBounds, fullResolution);
}

FileFilm *CreateFileFilm(const ParamSet &params,
                         std::unique_ptr<Filter> filter) {
    std::string filename;
    if (PbrtOptions.imageFile != "") {
        filename = PbrtOptions.imageFile;
        std::string paramsFilename = params.FindOneString("filename", "");
        if (paramsFilename != "")
            Warning(
                "Output filename supplied on command line, \"%s\" is overriding "
                "filename provided in scene description file, \"%s\".",
                PbrtOptions.imageFile.c_str(), paramsFilename.c_str());
    } else
        filename = params.FindOneString("filename", "pbrt.exr");

    int xres = params.FindOneInt("xresolution", 1280);
    int yres = params.FindOneInt("yresolution", 720);
    if (PbrtOptions.quickRender) xres = std::max(1, xres / 4);
    if (PbrtOptions.quickRender) yres = std::max(1, yres / 4);
    Bounds2f crop;
    int cwi;
    const Float *cr = params.FindFloat("cropwindow", &cwi);
    if (cr && cwi == 4) {
        crop.pMin.x = Clamp(std::min(cr[0], cr[1]), 0.f, 1.f);
        crop.pMax.x = Clamp(std::max(cr[0], cr[1]), 0.f, 1.f);
        crop.pMin.y = Clamp(std::min(cr[2], cr[3]), 0.f, 1.f);
        crop.pMax.y = Clamp(std::max(cr[2], cr[3]), 0.f, 1.f);
    } else if (cr)
        Error("%d values supplied for \"cropwindow\". Expected 4.", cwi);
    else
        crop = Bounds2f(Point2f(Clamp(PbrtOptions.cropWindow[0][0], 0, 1),
                                Clamp(PbrtOptions.cropWindow[1][0], 0, 1)),
                        Point2f(Clamp(PbrtOptions.cropWindow[0][1], 0, 1),
                                Clamp(PbrtOptions.cropWindow[1][1], 0, 1)));

    Float scale = params.FindOneFloat("scale", 1.);
    Float diagonal = params.FindOneFloat("diagonal", 35.);
    Float maxSampleLuminance = params.FindOneFloat("maxsampleluminance",
                                                   Infinity);
    return new FileFilm(Point2i(xres, yres), crop, std::move(filter), diagonal,
                    filename, scale, maxSampleLuminance);
}

}  // namespace pbrt
