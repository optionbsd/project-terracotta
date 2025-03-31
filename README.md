# Project Terracotta  

<p align="center">  
  <img src="Terracotta.png" alt="Terracotta Logo" width="400">  
</p>  

<p align="center">  
  Project Terracotta  
</p>  

**Project Terracotta** is a project aimed at replacing X11 on old laptops and PCs running FreeBSD.  

## Building (for FreeBSD only)  

  - Download the sources from the `unstable` branch.  
  - Go to the `project-terracotta` directory.  
  - Run: `sudo pkg install drm-kmod gmake`.  
  - Finally, run: `make`.  

## Running Project Terracotta  

  - Run: `sudo build/terracotta` (make sure you are in the `project-terracotta` directory).  

## Platforms  

Here are the platforms where `Project Terracotta` has been tested:  


✅ - Supported  
❌ - Not supported  
❓ - Testing; it might work, but it's not guaranteed  


  - `FreeBSD 14.1 i386` - ✅  
  - `FreeBSD 14.1 x86_64` - ❓  
  - `FreeBSD 14.1 aarch64` - ❌
