#!/bin/bash
set -eEo pipefail
shopt -s failglob
trap 'echo "${BASH_SOURCE[0]}{${FUNCNAME[0]}}:${LINENO}: Error: command \`${BASH_COMMAND}\` failed with exit code $?"' ERR

if [ "$HOSTTYPE" = "aarch64" ]
then
	JETSON=1
fi

# Adjust as needed
OPENCV_VERSION="4.6.0"
if [ $JETSON ]
then
	CUDA_VERSION="10-2"
else
	CUDA_VERSION="11-4"
fi

echo "Installing dependencies..."
sudo apt update
sudo apt install -y \
	build-essential \
	cmake \
	curl \
	libgtkglext1-dev \
	libjpeg-dev \
	libopenexr-dev \
	libopenjp2-7-dev \
	libpng-dev \
	libtiff-dev \
	libwebp-dev \
	pkg-config \
	unzip
if [ $JETSON ]
then
	sudo mkdir -p /usr/lib/aarch64-linux-gnu/gtkglext-1.0/include
	sudo apt install -y cuda-toolkit-${CUDA_VERSION}
else
	sudo apt install -y cuda-minimal-build-${CUDA_VERSION} cuda-libraries-dev-${CUDA_VERSION}
fi
echo "Done."

mkdir opencv
cd opencv

echo "Downloading OpenCV and Contribs Modules..."
curl -L https://github.com/opencv/opencv/archive/${OPENCV_VERSION}.zip -o opencv-${OPENCV_VERSION}.zip
echo "Downloaded opencv-${OPENCV_VERSION}.zip."
curl -L https://github.com/opencv/opencv_contrib/archive/${OPENCV_VERSION}.zip -o opencv_contrib-${OPENCV_VERSION}.zip
echo "Downloaded opencv_contrib-${OPENCV_VERSION}.zip."

echo "Unpacking..."
unzip opencv-${OPENCV_VERSION}.zip
unzip opencv_contrib-${OPENCV_VERSION}.zip
echo "Done."

echo "Configuring modules..."
mkdir build_release
cd build_release
CONFIG="-DOPENCV_EXTRA_MODULES_PATH=../opencv_contrib-${OPENCV_VERSION}/modules \
	-DBUILD_TESTS=OFF \
	-DBUILD_PERF_TESTS=OFF \
	-DWITH_GTK_2_X=ON \
	-DWITH_OPENGL=ON \
	-DWITH_CUDA=ON \
	-DCUDA_FAST_MATH=ON"
if [ $JETSON ]
then
	CONFIG+=" -DCUDA_ARCH_BIN=5.3,6.2,7.2 -DCUDA_ARCH_PTX=7.2"
fi
cmake $CONFIG ../opencv-${OPENCV_VERSION}
echo "Configuration done."

echo "Building..."
make -j8
echo "Installing to /usr/local ..."
sudo cmake -P cmake_install.cmake
echo "OpenCV ${OPENCV_VERSION} has been successfully installed."
