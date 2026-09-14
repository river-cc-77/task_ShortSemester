/** 图表与 KPI 数值格式化 */

export function round2(v) {
  if (v == null || v === '') return 0
  return Math.round(Number(v) * 100) / 100
}

export function fmtMoney(v) {
  if (v == null || v === '') return '-'
  return round2(v).toFixed(2)
}

export function axisMoney(v) {
  return round2(v).toFixed(2)
}

export const tooltipMoney = {
  trigger: 'axis',
  valueFormatter: (v) => (v == null ? '-' : `${round2(v).toFixed(2)} 元`),
}

export const tooltipAxis2 = {
  trigger: 'axis',
  valueFormatter: (v) => (v == null ? '-' : round2(v).toFixed(2)),
}
