set -e

plat='host'
if [[ $# -gt 0 ]]; then
    plat='cuda'
fi

cur=$(realpath $(dirname $0))
o3d=$(realpath $(dirname $0)/..)
bd=$o3d/release_$plat

export http_proxy=http://127.0.0.1:7890
export https_proxy=http://127.0.0.1:7890


options=""
if [[ $plat == "cuda" ]]; then
    options="$options -DBUILD_CUDA_MODULE=ON"
fi

rm -rf $bd && mkdir $bd
pushd $bd > /dev/null
cmake $options $o3d
make pip-package -j 12
pkg=$(ls $bd/lib/python_package/pip_package/open3d*.whl)
popd > /dev/null

echo "open3d for $plat is built: $pkg"

