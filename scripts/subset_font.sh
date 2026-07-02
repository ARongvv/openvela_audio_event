#!/usr/bin/env bash
# Subset MiSans font to only include needed characters.
# Usage: bash scripts/subset_font.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
FONT_DIR="${REPO_ROOT}/contest2026_031_niudanxianqianchong/demos/smart_home/res/fonts"
CHARS_FILE="${FONT_DIR}/chars.txt"

# ---- Build character set ----
# ASCII printable + common CJK for smart home UI
cat > "${CHARS_FILE}" << 'EOF'
ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,;:!?-+=%()[]{}<>/@#$&*_~"'`|
打开关闭客厅卧室灯光亮度温度湿度空调风扇窗帘场景睡眠起床回家离家设置中英文确认取消返回加载中连接失败重试网络异常开关调节大小高低快慢冷暖上下左右前后多少是不是有没有能不能空调暖气新风地暖热水器洗衣机冰箱电视投影仪扫地机加湿器净化器排气扇浴霸窗帘纱窗门锁插座开关传感器烟雾报警水浸门窗人体红外摄像头门铃猫眼摄像头当前状态在线离线在线中已开已关正在模式制热制冷送风除湿自动定时小时分钟秒今天明天后天周一周二周三周四周五周六周日月年春夏秋冬晴阴雨雪风雾雷暴预警提醒通知消息语音文字图片视频文件链接二维码扫码设备房间区域楼层全屋主卧次卧儿童房书房厨房餐厅卫生间浴室阳台走廊玄关车库花园露台一楼二楼三楼地下室储物间洗衣房
EOF

echo "Character set: $(wc -m < "${CHARS_FILE}") characters"

# ---- Subset each font ----
for font in MiSans-Normal.ttf MiSans-Semibold.ttf; do
    src="${FONT_DIR}/${font}"
    dst="${FONT_DIR}/${font%.ttf}-subset.ttf"

    if [[ ! -f "${src}" ]]; then
        echo "SKIP: ${src} not found"
        continue
    fi

    echo "Subsetting ${font}..."
    python3 -m fontTools.subset \
        "${src}" \
        --text-file="${CHARS_FILE}" \
        --output-file="${dst}" \
        --layout-features='*' \
        --no-hinting \
        --desubroutinize

    echo "  ${font}: $(du -h "${src}" | cut -f1) -> $(du -h "${dst}" | cut -f1)"
done

echo ""
echo "Done. Subset fonts are in ${FONT_DIR}/"
