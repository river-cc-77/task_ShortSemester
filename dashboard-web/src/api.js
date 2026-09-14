const ENDPOINTS = [
  'kpi',
  'revenue_trend',
  'pile_status',
  'station_hourly_today',
  'station_rank',
  'load_forecast',
  'time_forecast',
  'hourly_history',
  'weekday_weekend',
  'station_util',
  'region_stats',
  'station_hour_matrix',
]

export async function fetchAll() {
  const results = await Promise.all(
    ENDPOINTS.map(async (name) => {
      const resp = await fetch(`/api/${name}`)
      if (!resp.ok) throw new Error(`/api/${name} failed`)
      return [name, await resp.json()]
    })
  )
  return Object.fromEntries(results)
}
