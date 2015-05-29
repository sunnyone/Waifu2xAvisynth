# waifu2xAvisynth
waifu2x for Avisynth plugin.

It is based on [waifu2x-converter-cpp](https://github.com/WL-Amigo/waifu2x-converter-cpp), also it's a reimplementation of waifu2x ([original](https://github.com/nagadomi/waifu2x)).

See http://waifu2x-avisynth.sunnyone.org/ in detail.

# Requires

* Visual Studio 2013 runtime.
* Avisynth 2.6 x86.

# Usage
Copy waifu2xAvisynth.dll and models to a folder.

## Simple example
```
Waifu2x(nr=1, scale=2)
```

## Detailed example
```
LoadPlugin("C:\opt\waifu2xAvisynth\waifu2xAvisynth.dll")
Waifu2x(nr=1, scale=2, models="C:\opt\waifu2xAvisynth-models\models", jobs=1)
```

# Parameters
* nr: noise reduction level. 1 or 2 can be used. 0 means disabled. default is 1.
* scale: scaling ratio. 2, 4, 8, ... can be used. 1 means disabled. default is 2.
* models: the path of "models" folder. default is the folder of dll.
* jobs: job count. default is core number.
