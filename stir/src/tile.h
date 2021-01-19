/* -*- mode: C; c-basic-offset: 4; intent-tabs-mode: nil -*-
 *
 * STIR -- Sifteo Tiled Image Reducer
 * Micah Elizabeth Scott <micah@misc.name>
 *
 * Copyright <c> 2011 Sifteo, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _TILE_H
#define _TILE_H

#include <stdint.h>
#include <float.h>
#include <string.h>
#include <memory>
#include <unordered_set>
#include <unordered_map>

#include "color.h"
#include "logger.h"

namespace Stir {

class Tile;
class TileStack;
typedef std::shared_ptr<Tile> TileRef;


/*
 * TilePalette --
 *
 *    The color palette for one single tile. This is a small utility
 *    object that we use as part of the tile order optimization, in
 *    order to search for runs of tiles with common colors.
 *
 *    Tiles will only have indexed color palettes if we have
 *    LUT_MAX or fewer distinct colors. If numColors is greater
 *    than LUT_MAX, the colors[] array is not valid.
 *
 *    We keep colors[] ordered by decreasing popularity. See
 *    Tile::constructPalette().
 */

struct TilePalette {
    TilePalette();

    static const unsigned LUT_MAX = 16;

    uint8_t numColors;
    RGB565 colors[LUT_MAX];

    enum ColorMode {
        CM_INVALID = -1,

        CM_LUT1,
        CM_LUT2,
        CM_LUT4,
        CM_LUT16,
        CM_TRUE,

        CM_COUNT,
    };

    static const char *colorModeName(ColorMode m);

    ColorMode colorMode() const {
        if (numColors <= 1)  return CM_LUT1;
        if (numColors <= 2)  return CM_LUT2;
        if (numColors <= 4)  return CM_LUT4;
        if (numColors <= 16) return CM_LUT16;
        return CM_TRUE;
    }

    unsigned maxLUTIndex() const {
        if (numColors <= 1)  return 15;  // Solid-color opcode can reach any LUT entry
        if (numColors <= 2)  return 1;
        if (numColors <= 4)  return 3;
        return 15;
    }

    bool hasLUT() const {
        // Do we have a color LUT at all?
        return numColors <= LUT_MAX;
    }
};


/*
 * TileOptions --
 *
 *    Per-tile options that affect the behavior of the optimizer.
 */

struct TileOptions {
    TileOptions(double _quality=0, bool _pinned=false)
        : quality(_quality), pinned(_pinned), chromaKey(true)
        {}

    double getMaxMSE() const;
    
    double quality;
    bool pinned;
    bool chromaKey;
    
    bool operator== (const TileOptions &other) const {
        return quality == other.quality &&
               pinned == other.pinned &&
               chromaKey == other.chromaKey;
    }
};


/*
 * Tile --
 *
 *    One fixed-size image tile, in palettized RGB565 color. 
 *
 *    Tile objects are immutable after they are initially created.
 *    Tiles are flyweighted; any tiles that have an identical
 *    TileData will share the same Tile instance.
 */

class Tile {
 public:
    static const unsigned SIZE = 8;       // Number of pixels on a side
    static const unsigned PIXELS = 64;    // Total pixels in a tile

    // Chroma key constants, defined by our ABI.
    static const uint8_t CHROMA_KEY    = 0x4F;
    static const uint8_t CKEY_BIT_EOL  = 0x40;

    // Unique key that identifies a tile
    struct Identity {
        RGB565 pixels[PIXELS];
        TileOptions options;
        
        bool operator== (const Identity &other) const {
            return options == other.options && !memcmp(pixels, other.pixels, sizeof pixels);
        }
    };

    static TileRef instance(const Identity &id);
    static TileRef instance(const TileOptions &opt, uint8_t *rgba, size_t stride);
    
    RGB565 pixel(unsigned i) const {
        return mID.pixels[i];
    }

    RGB565 pixel(unsigned x, unsigned y) const {
        return pixel(x + y * SIZE);
    }

    RGB565 pixelWrap(unsigned x, unsigned y) const {
        return pixel(x & 7, y & 7);
    }

    const TilePalette &palette() {
        // Lazily build the palette info
        if (!mPalette.numColors)
            constructPalette();
        return mPalette;
    }   

    const TileOptions &options() const {
        return mID.options;
    }

    double errorMetric(Tile &other, double limit=DBL_MAX);

    double fineMSE(Tile &other); 
    double coarseMSE(Tile &other);
    double sobelError(Tile &other);

    TileRef reduce(ColorReducer &reducer) const;

 private:
    Tile(const Identity &id);

    static std::unordered_map<Identity, TileRef> instances;
    
    void constructPalette();
    void constructSobel();
    void constructDec4();

    friend class TileStack;
    
    bool mHasSobel;
    bool mHasDec4;
    TilePalette mPalette;
    Identity mID;
    CIELab mDec4[4];
    double mSobelGx[PIXELS];
    double mSobelGy[PIXELS];
    double mSobelTotal;
};


/*
 * TileStack --
 *
 *    A stack of similar tiles, represented at any given time by a tile
 *    created via a per-pixel median operation on every tile in the set.
 *
 *    When the optimizer finds a tile that's similar to this set, it
 *    can add it to the set in order to statistically incorporate that
 *    tile's pixels into the median image we'll eventually generate
 *    for that set of tiles.
 */

class TileStack {
 public:
    TileStack();

    void add(TileRef t);
    void replace(TileRef t);

    inline __attribute__ ((always_inline)) TileRef median()
    {
        if (!cache) {
            if (tiles.size() == 1) {
                // Special-case for a single-tile stack. No copy, just add a reference
                cache = TileRef(tiles[0]);
            } else {
                // General-case median algorithm
                computeMedian();
            }
        }
        return cache;
    }

    bool isPinned() const {
        return mPinned;
    }

    bool isLossless() const {
        return mLossless;
    }

 private:
    static const unsigned MAX_SIZE = 128;
    static const unsigned NO_INDEX = (unsigned)-1;

    friend class TilePool;

    std::vector<TileRef> tiles;
    TileRef cache;
    unsigned index;
    bool mPinned;
    bool mLossless;

    void computeMedian();
};


/*
 * TilePool --
 *
 *    An independent pool of tiles, supporting lossless or lossy optimization.
 */

class TilePool {
 public:
    typedef uint32_t Serial;
    typedef uint16_t Index;

    // Current value of SysLFS::TILES_PER_ASSET_SLOT from firmware
    static const unsigned MAX_SIZE = 4096;

    TilePool() : numFixed(0) {}

    // Normal optimization flow
    void optimize(Logger &log);
    void encode(std::vector<uint8_t>& out, Logger *log = NULL);

    // All previous tiles are set in stone, no new tiles can be added
    void makeFixed() {
        numFixed = tiles.size();
    }

    Serial add(TileRef t) {
        Serial s = (Serial)tiles.size();
        tiles.push_back(t);
        return s;
    }

    Index index(Serial s) const {
        // Get the index of an optimized tile image, by serial number
        return stackIndex[s]->index;
    }

    TileRef tile(Index s) const {
        // Get a tile image, from the zero-based index
        return stackArray[s]->median();
    }

    unsigned size() const {
        // Size of the optimized tile pool
        return stackList.size();
    }

    uint8_t rawByte(uint32_t addr) const
    {
        /*
         * Look up a raw byte of decompressed tile data, by its byte address.
         * Out-of-range addresses return 0xFF.
         *
         * NB: This matches the actual endianness that our cube firmware
         *     uses for flash storage, i.e. big endian (to match the display)
         */

        Index ti = addr / (sizeof(RGB565) * Tile::PIXELS);
        unsigned pi = (addr / sizeof(RGB565)) % Tile::PIXELS;
        if (ti < size())
            return tile(ti)->pixel(pi).value >> (((addr & 1) ^ 1) << 3);
        return 0xFF;
    }

    void calculateCRC(std::vector<uint8_t> &crcbuf) const;

 private:
    unsigned numFixed;

    std::list<TileStack> stackList;       // Reorderable list of all stacked tiles
    std::vector<TileStack*> stackArray;   // Vector version of 'stackList', built after indices are known.
    std::vector<TileRef> tiles;           // Current best image for each tile, by Serial
    std::vector<TileStack*> stackIndex;   // Current optimized stack for each tile, by Serial
 
    void optimizeFixedTiles(Logger &log);
    void optimizePalette(Logger &log);
    void optimizeOrder(Logger &log);
    void optimizeTiles(Logger &log);
    void optimizeTrueColorTiles(Logger &log);
    void optimizeTilesPass(Logger &log,
                           std::unordered_set<TileStack *> &activeStacks,
                           bool gather, bool pinned);

    TileStack *closest(TileRef t, double distance);
};


/*
 * TileGrid --
 *
 *    An image, converted into a matrix of TileStack references.
 */

class TileGrid {
 public:
    TileGrid(TilePool *pool);

    void load(const TileOptions &opt, uint8_t *rgba,
              size_t stride, unsigned width, unsigned height);

    unsigned width() const {
        return mWidth;
    }

    unsigned height() const {
        return mHeight;
    }

    TilePool::Serial tile(unsigned x, unsigned y) const {
        return tiles[x + y * mWidth];
    }

    const TilePool &getPool() const {
        return *mPool;
    }

 private:
    TilePool *mPool;
    unsigned mWidth, mHeight;
    std::vector<TilePool::Serial> tiles;
};

};  // namespace Stir

#endif
