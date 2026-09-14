<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import VChart from 'vue-echarts'
import { use } from 'echarts/core'
import { CanvasRenderer } from 'echarts/renderers'
import { BarChart, LineChart, PieChart, HeatmapChart, RadarChart, GaugeChart, ScatterChart } from 'echarts/charts'
import {
  GridComponent,
  TooltipComponent,
  LegendComponent,
  VisualMapComponent,
  RadarComponent,
} from 'echarts/components'
import { fetchAll } from './api.js'

use([
  CanvasRenderer,
  BarChart,
  LineChart,
  PieChart,
  HeatmapChart,
  RadarChart,
  GaugeChart,
  ScatterChart,
  GridComponent,
  TooltipComponent,
  LegendComponent,
  VisualMapComponent,
  RadarComponent,
])

const data = ref(null)
const error = ref('')
const updatedAt = ref('')

const kpiCards = computed(() => {
  if (!data.value?.kpi) return []
  const k = data.value.kpi
  const d = k.latest_daily || {}
  return [
    ['充电站', k.station_count ?? '-'],
    ['电桩总数', k.pile_count ?? '-'],
    ['空闲桩', k.idle_piles ?? '-'],
    ['日营收(元)', d.total_revenue != null ? Number(d.total_revenue).toFixed(2) : '-'],
    ['日订单', d.order_count ?? '-'],
  ]
})

const revenueOpt = computed(() => {
  const rows = data.value?.revenue_trend || []
  return {
    backgroundColor: 'transparent',
    tooltip: { trigger: 'axis' },
    grid: { left: 48, right: 16, top: 24, bottom: 28 },
    xAxis: { type: 'category', data: rows.map((r) => r.stat_date), axisLabel: { color: '#94a3b8' } },
    yAxis: { type: 'value', axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: '#1e293b' } } },
    series: [
      {
        name: '营收',
        type: 'line',
        smooth: true,
        areaStyle: { color: 'rgba(56,189,248,0.25)' },
        data: rows.map((r) => r.total_revenue),
        color: '#38bdf8',
      },
    ],
  }
})

const pileOpt = computed(() => ({
  backgroundColor: 'transparent',
  tooltip: { trigger: 'item' },
  legend: { bottom: 0, textStyle: { color: '#cbd5e1' } },
  series: [
    {
      type: 'pie',
      radius: ['42%', '68%'],
      roseType: 'area',
      data: (data.value?.pile_status || []).map((r) => ({ name: r.status, value: r.cnt })),
      label: { color: '#e2e8f0' },
    },
  ],
}))

const hourlyOpt = computed(() => {
  const rows = data.value?.station_hourly_today || []
  return {
    backgroundColor: 'transparent',
    tooltip: { trigger: 'axis' },
    legend: { data: ['kWh', '订单'], textStyle: { color: '#cbd5e1' } },
    grid: { left: 48, right: 16, top: 36, bottom: 28 },
    xAxis: { type: 'category', data: rows.map((r) => `${r.stat_hour}:00`), axisLabel: { color: '#94a3b8' } },
    yAxis: [
      { type: 'value', axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: '#1e293b' } } },
      { type: 'value', axisLabel: { color: '#94a3b8' }, splitLine: { show: false } },
    ],
    series: [
      { name: 'kWh', type: 'bar', data: rows.map((r) => r.kwh), color: '#22c55e' },
      { name: '订单', type: 'line', yAxisIndex: 1, data: rows.map((r) => r.orders), color: '#f59e0b' },
    ],
  }
})

const rankOpt = computed(() => {
  const rows = [...(data.value?.station_rank || [])].reverse()
  return {
    backgroundColor: 'transparent',
    tooltip: { trigger: 'axis' },
    grid: { left: 96, right: 16, top: 16, bottom: 28 },
    xAxis: { type: 'value', axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: '#1e293b' } } },
    yAxis: { type: 'category', data: rows.map((r) => r.name), axisLabel: { color: '#94a3b8' } },
    series: [{ type: 'bar', data: rows.map((r) => r.revenue), color: '#818cf8' }],
  }
})

const historyOpt = computed(() => {
  const hours = Array.from({ length: 24 }, (_, i) => i)
  const rows = data.value?.hourly_history || []
  const kwh = hours.map((h) => Number(rows.find((r) => Number(r.stat_hour) === h)?.kwh || 0))
  return {
    backgroundColor: 'transparent',
    tooltip: { trigger: 'axis' },
    grid: { left: 48, right: 16, top: 24, bottom: 28 },
    xAxis: { type: 'category', data: hours.map((h) => `${h}:00`), axisLabel: { color: '#94a3b8' } },
    yAxis: { type: 'value', axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: '#1e293b' } } },
    series: [{ name: '历史 kWh', type: 'bar', data: kwh, color: '#06b6d4' }],
  }
})

const weekdayOpt = computed(() => {
  const rows = data.value?.weekday_weekend || []
  return {
    backgroundColor: 'transparent',
    tooltip: { trigger: 'axis' },
    legend: { data: ['kWh', '订单'], textStyle: { color: '#cbd5e1' } },
    grid: { left: 48, right: 16, top: 36, bottom: 28 },
    xAxis: { type: 'category', data: rows.map((r) => r.label || r.day_type), axisLabel: { color: '#94a3b8' } },
    yAxis: [
      { type: 'value', axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: '#1e293b' } } },
      { type: 'value', axisLabel: { color: '#94a3b8' }, splitLine: { show: false } },
    ],
    series: [
      { name: 'kWh', type: 'bar', data: rows.map((r) => r.kwh), color: '#6366f1' },
      { name: '订单', type: 'line', yAxisIndex: 1, data: rows.map((r) => r.orders), color: '#f97316' },
    ],
  }
})

const heatmapOpt = computed(() => {
  const rows = data.value?.station_hour_matrix || []
  const stations = [...new Set(rows.map((r) => r.station_name))]
  const hours = Array.from({ length: 24 }, (_, i) => i)
  const matrix = rows.map((r) => [hours.indexOf(Number(r.stat_hour)), stations.indexOf(r.station_name), Number(r.kwh)])
  const maxVal = Math.max(...matrix.map((m) => m[2]), 1)
  return {
    backgroundColor: 'transparent',
    tooltip: { position: 'top' },
    grid: { left: 88, right: 48, top: 16, bottom: 48 },
    xAxis: { type: 'category', data: hours.map((h) => `${h}h`), splitArea: { show: true }, axisLabel: { color: '#94a3b8' } },
    yAxis: { type: 'category', data: stations, splitArea: { show: true }, axisLabel: { color: '#94a3b8' } },
    visualMap: {
      min: 0,
      max: maxVal,
      calculable: true,
      orient: 'horizontal',
      left: 'center',
      bottom: 0,
      inRange: { color: ['#0f172a', '#0369a1', '#38bdf8', '#fde047'] },
      textStyle: { color: '#94a3b8' },
    },
    series: [{ type: 'heatmap', data: matrix, label: { show: false } }],
  }
})

const radarOpt = computed(() => {
  const rows = data.value?.station_util || []
  if (!rows.length) return {}
  const indicators = [
    { name: '利用率', max: 1 },
    { name: '周转率', max: Math.max(...rows.map((r) => r.avg_turnover || 0), 1) },
    { name: '故障率', max: Math.max(...rows.map((r) => r.fault_rate || 0), 0.1) },
  ]
  return {
    backgroundColor: 'transparent',
    tooltip: {},
    legend: { data: rows.slice(0, 4).map((r) => r.name), bottom: 0, textStyle: { color: '#cbd5e1' } },
    radar: {
      indicator: indicators,
      axisName: { color: '#94a3b8' },
      splitLine: { lineStyle: { color: '#1e293b' } },
      splitArea: { areaStyle: { color: ['rgba(15,23,42,0.4)', 'rgba(30,41,59,0.4)'] } },
    },
    series: [
      {
        type: 'radar',
        data: rows.slice(0, 4).map((r) => ({
          name: r.name,
          value: [r.avg_util, r.avg_turnover, r.fault_rate],
        })),
      },
    ],
  }
})

const regionOpt = computed(() => {
  const rows = data.value?.region_stats || []
  return {
    backgroundColor: 'transparent',
    tooltip: { trigger: 'item' },
    series: [
      {
        type: 'pie',
        radius: ['20%', '65%'],
        center: ['50%', '52%'],
        data: rows.map((r) => ({ name: r.region || '其他', value: r.revenue })),
        label: { color: '#e2e8f0' },
      },
    ],
  }
})

const loadOpt = computed(() => {
  const rows = data.value?.load_forecast || []
  const horizons = ['1h', '6h', '24h']
  const stations = [...new Set(rows.map((r) => r.station_name))]
  return {
    backgroundColor: 'transparent',
    tooltip: { trigger: 'axis' },
    legend: { data: horizons, textStyle: { color: '#cbd5e1' } },
    grid: { left: 48, right: 16, top: 36, bottom: 72 },
    xAxis: { type: 'category', data: stations, axisLabel: { color: '#94a3b8', rotate: 18 } },
    yAxis: { type: 'value', axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: '#1e293b' } } },
    series: horizons.map((h) => ({
      name: h,
      type: 'bar',
      data: stations.map((name) => {
        const row = rows.find((r) => r.station_name === name && r.horizon === h)
        return row ? row.predicted_load : 0
      }),
    })),
  }
})

const timeOpt = computed(() => {
  const items = (data.value?.time_forecast || []).filter((r) => r.horizon === '1h')
  return {
    backgroundColor: 'transparent',
    tooltip: { trigger: 'axis' },
    legend: { data: ['时长(分)', '高峰时'], textStyle: { color: '#cbd5e1' } },
    grid: { left: 48, right: 48, top: 36, bottom: 72 },
    xAxis: { type: 'category', data: items.map((r) => r.station_name), axisLabel: { color: '#94a3b8', rotate: 18 } },
    yAxis: [
      { type: 'value', axisLabel: { color: '#94a3b8' }, splitLine: { lineStyle: { color: '#1e293b' } } },
      { type: 'value', min: 0, max: 23, axisLabel: { color: '#94a3b8' }, splitLine: { show: false } },
    ],
    series: [
      { name: '时长(分)', type: 'bar', data: items.map((r) => r.predicted_avg_duration_min), color: '#14b8a6' },
      { name: '高峰时', type: 'scatter', yAxisIndex: 1, data: items.map((r) => r.predicted_peak_hour ?? 0), color: '#fb7185' },
    ],
  }
})

const gaugeOpt = computed(() => {
  const util = data.value?.kpi?.latest_daily?.utilization
  const val = util != null ? Math.round(Number(util) * 100) : 0
  return {
    backgroundColor: 'transparent',
    series: [
      {
        type: 'gauge',
        min: 0,
        max: 100,
        progress: { show: true, width: 12 },
        axisLine: { lineStyle: { width: 12, color: [[1, '#1e293b']] } },
        axisLabel: { color: '#94a3b8' },
        detail: { valueAnimation: true, formatter: '{value}%', color: '#38bdf8', fontSize: 22 },
        data: [{ value: val, name: '平台利用率' }],
        title: { color: '#94a3b8' },
      },
    ],
  }
})

const evalLoad = computed(() => data.value?.ml_evaluation?.load_kwh || {})
const evalDuration = computed(() => data.value?.ml_evaluation?.duration_min || {})

async function refresh() {
  try {
    data.value = await fetchAll()
    updatedAt.value = data.value.kpi?.updated_at || new Date().toLocaleString()
    error.value = ''
  } catch (e) {
    error.value = e.message
  }
}

let timer
onMounted(() => {
  refresh()
  timer = setInterval(refresh, 60000)
})
onUnmounted(() => clearInterval(timer))
</script>

<template>
  <div class="dashboard">
    <header class="header">
      <div>
        <dv-decoration-5 :dur="3" style="width: 420px; height: 36px" />
        <h1>东软电动汽车充电桩 — 机器学习智能分析大屏</h1>
        <div class="sub">Vue3 · DataV · Flask API · PySpark 多维分析 · WMA 负荷预测</div>
      </div>
      <div class="time">{{ error || `刷新：${updatedAt}` }}</div>
    </header>

    <section class="kpi-row">
      <dv-border-box-8 v-for="([label, value], i) in kpiCards" :key="i" class="kpi-box">
        <div class="kpi-label">{{ label }}</div>
        <div class="kpi-value">{{ value }}</div>
      </dv-border-box-8>
    </section>

    <section class="grid">
      <dv-border-box-1 class="panel">
        <div class="panel-title">近 30 日营收趋势（折线/面积）</div>
        <VChart class="chart" :option="revenueOpt" autoresize />
      </dv-border-box-1>

      <dv-border-box-1 class="panel">
        <div class="panel-title">电桩状态（玫瑰饼图）</div>
        <VChart class="chart" :option="pileOpt" autoresize />
      </dv-border-box-1>

      <dv-border-box-1 class="panel">
        <div class="panel-title">平台利用率（仪表盘）</div>
        <VChart class="chart" :option="gaugeOpt" autoresize />
      </dv-border-box-1>

      <dv-border-box-12 class="panel wide">
        <div class="panel-title">充电高峰曲线（柱+折线双轴）</div>
        <VChart class="chart" :option="hourlyOpt" autoresize />
      </dv-border-box-12>

      <dv-border-box-1 class="panel">
        <div class="panel-title">区域营收分布（环形饼图）</div>
        <VChart class="chart" :option="regionOpt" autoresize />
      </dv-border-box-1>

      <dv-border-box-1 class="panel">
        <div class="panel-title">电站排行（横向柱状）</div>
        <VChart class="chart" :option="rankOpt" autoresize />
      </dv-border-box-1>

      <dv-border-box-1 class="panel">
        <div class="panel-title">24h 历史分布（柱状）</div>
        <VChart class="chart" :option="historyOpt" autoresize />
      </dv-border-box-1>

      <dv-border-box-13 class="panel wide">
        <div class="panel-title">交叉对比 1：工作日 vs 周末</div>
        <VChart class="chart" :option="weekdayOpt" autoresize />
      </dv-border-box-13>

      <dv-border-box-13 class="panel full">
        <div class="panel-title">交叉对比 2：电站 × 小时热力矩阵</div>
        <VChart class="chart tall" :option="heatmapOpt" autoresize />
      </dv-border-box-13>

      <dv-border-box-1 class="panel">
        <div class="panel-title">电站运营雷达图</div>
        <VChart class="chart" :option="radarOpt" autoresize />
      </dv-border-box-1>

      <dv-border-box-12 class="panel wide">
        <div class="panel-title">各站负荷预测 1h/6h/24h</div>
        <VChart class="chart" :option="loadOpt" autoresize />
      </dv-border-box-12>

      <dv-border-box-12 class="panel wide">
        <div class="panel-title">充电时长与高峰（柱+散点）</div>
        <VChart class="chart" :option="timeOpt" autoresize />
      </dv-border-box-12>

      <dv-border-box-8 class="panel full">
        <div class="panel-title">WMA 模型离线评估（MAE / RMSE / MAPE）</div>
        <div class="eval-row">
          <div class="eval-item">
            <div class="label">负荷 MAE (kWh)</div>
            <div class="value">{{ evalLoad.mae ?? '—' }}</div>
          </div>
          <div class="eval-item">
            <div class="label">负荷 RMSE</div>
            <div class="value">{{ evalLoad.rmse ?? '—' }}</div>
          </div>
          <div class="eval-item">
            <div class="label">负荷 MAPE (%)</div>
            <div class="value">{{ evalLoad.mape_pct ?? '—' }}</div>
          </div>
          <div class="eval-item">
            <div class="label">时长 MAE (min)</div>
            <div class="value">{{ evalDuration.mae ?? '—' }}</div>
          </div>
          <div class="eval-item">
            <div class="label">时长 RMSE</div>
            <div class="value">{{ evalDuration.rmse ?? '—' }}</div>
          </div>
          <div class="eval-item">
            <div class="label">时长 MAPE (%)</div>
            <div class="value">{{ evalDuration.mape_pct ?? '—' }}</div>
          </div>
        </div>
      </dv-border-box-8>
    </section>
  </div>
</template>
