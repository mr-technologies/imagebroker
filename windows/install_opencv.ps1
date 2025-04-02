# Adjust the following settings as needed
$VS_INSTALL_PATH="${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
$OPENCV_VERSION="4.10.0"
$WITH_CUDA="ON"

$ErrorActionPreference="Stop"
pushd "${PSScriptRoot}"
$failed=$true
try
{
	mkdir build

	"Installing Python numpy..."
	python -m pip install --upgrade pip
	pip install --upgrade numpy

	Import-Module "${VS_INSTALL_PATH}\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
	Enter-VsDevShell -VsInstallPath "${VS_INSTALL_PATH}" -DevCmdArguments "-arch=x64 -host_arch=x64"

	"Downloading OpenCV and Contribs Modules..."
	Invoke-WebRequest -Uri "https://github.com/opencv/opencv/archive/${OPENCV_VERSION}.zip" -OutFile "build\opencv-${OPENCV_VERSION}.zip"
	"Downloaded opencv-${OPENCV_VERSION}.zip."
	Invoke-WebRequest -Uri "https://github.com/opencv/opencv_contrib/archive/${OPENCV_VERSION}.zip" -OutFile "build\opencv_contrib-${OPENCV_VERSION}.zip"
	"Downloaded opencv_contrib-${OPENCV_VERSION}.zip."
	"Unpacking..."
	Expand-Archive -Path "build\opencv-${OPENCV_VERSION}.zip" -DestinationPath build
	Expand-Archive -Path "build\opencv_contrib-${OPENCV_VERSION}.zip" -DestinationPath build
	"Done."

	"Configuring modules..."
	mkdir build\release
	cmake -DCMAKE_INSTALL_PREFIX=install -DCMAKE_BUILD_TYPE=Release "-DOPENCV_EXTRA_MODULES_PATH=build/opencv_contrib-${OPENCV_VERSION}/modules" -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DWITH_OPENGL=ON "-DWITH_CUDA=${WITH_CUDA}" -DCUDA_FAST_MATH=ON -G Ninja -B build\release "build\opencv-${OPENCV_VERSION}"
	"Configuration done."

	"Building..."
	cmake --build build\release
	"Installing..."
	cmake --install build\release

	$failed=$false
	"OpenCV ${OPENCV_VERSION} has been successfully installed."
	"You can now remove ${PWD}\build."
	"Set OpenCV_DIR environment variable to: ${PWD}\install"
}
catch
{
	$_
}
finally
{
	popd
	if($failed)
	{
		Read-Host "Failed! Remove ${PWD}\build before trying again. Press Enter to exit"
	}
	else
	{
		Read-Host "Success! Press Enter to exit"
	}
}
