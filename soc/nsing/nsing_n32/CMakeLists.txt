# Nations Technologies N32 SoC family

# Copyright (c) 2024 Nations Technologies
# SPDX-License-Identifier: Apache-2.0

# 全家族公共代码（nsing/common/）
add_subdirectory(common)

# Build only the selected N32 series.
add_subdirectory_ifdef(CONFIG_SOC_SERIES_N32G45X n32g45x)
add_subdirectory_ifdef(CONFIG_SOC_SERIES_N32H78X n32h78x)
