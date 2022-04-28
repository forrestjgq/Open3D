set -e

plat='host'
if [[ $# -gt 0 ]]; then
    plat='cuda'
fi

cur=$(realpath $(dirname $0))
o3d=$(realpath $(dirname $0)/..)
bd=$o3d/release_$plat

pushd $o3d > /dev/null
commit=$(git rev-parse HEAD |cut -b -6)
popd > /dev/null

pushd $o3d > /dev/null
pkg=$(ls $bd/lib/python_package/pip_package/open3d*.whl)

name="$(date '+%Y%m%d%H%M%S')_${commit}_${plat}"
mkdir $name
cp $pkg $name/

scp -r $name gqjiang@192.168.2.114:/data/jgq/ci/open3d/
ssh gqjiang@192.168.2.114 bash /data/jgq/ci/open3d/deploy.sh $plat $name
rm -rf $name

popd > /dev/null

echo "open3d for $plat is released"

