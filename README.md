# mod_convert

An AHTSE component (apache httpd module) to convert tile image formats


# Status

Only implicit conversion from JPEG w/Zen (8 or 12bit) to JPEG or PNG is supported currently

# Building

Requires apache httpd, libjpeg, libpng and libz to be available.  
For Linux, this means the runtime and the development packages have to be installed  
For the Windows VS solution, the headers are expected to be in the zlib, png and jpeg named folders in the same directory as the project files.  Apache runtime and development should be available under the /Apache24 folder

# Usage

Implements three apache httpd configuration directives:

## Convert_RegExp pattern
Can be used more than once, a request has to match at least one of the patterns before it is considered a mod_convert request

## Convert_ConfigurationFiles source_configuration main_configuration
The source_configuration should be the name of the file describing the AHTSE tile data source.  The main_configurtion contains directives controlling the output of the mod_convert.

## Convert_Indirect On
If set, the AHTSE convert module will not respond to normal requests, only to internal subrequests

# AHTSE directives that can appear both the source and the main configuration

## Size X Y Z C
- Mandatory entry, the size of the source image in pixels.  Z defaults to 1 and C defaults to 3

## PageSize X Y Z C
- Optional, pagesize of the source in pixels.  Defaults to 512 512 1 and Size:C

## SkippedLevels N
- Optional, defaults to 0.  How many levels at the top of the overview pyramid are not counted

## DataType Type
- Optional, defaults to Byte.  JPEG and PNG support Byte and UInt16

# Directives that appear in the main configuration only

## SourcePath path
- Mandatory, the location of the source, up to the first numerical argument, as a local web path

## SourcePostfix string
- Optional, a constant string literal that is appended to each request to the source

## EmptyTile size offset filename
- Optional, the file which is sent as the default (missing) tile.  When present, filename is required.  Offset defaults to 0 and size defaults to the size of the file.  If this directive is not present, a missing tile request will result in a HTTP not found (400) error.

## ETagSeed value
- Optional, a base 32 encoded 64bit value, used as a seed for ETag generation.  Defaults to 0
