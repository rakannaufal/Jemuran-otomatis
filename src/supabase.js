import { createClient } from "@supabase/supabase-js";

const supabaseUrl = "https://eebucdxywjmghzeoigry.supabase.co";
const supabaseKey =
  "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImVlYnVjZHh5d2ptZ2h6ZW9pZ3J5Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjUxNDI5OTcsImV4cCI6MjA4MDcxODk5N30.bmRzYO0ORFD8vOP1UeNnt-oEETkKCSYRBQneHOB2ueE";

export const supabase = createClient(supabaseUrl, supabaseKey);

// Function to save sensor data
export async function saveSensorData(data) {
  const { error } = await supabase.from("sensor_logs").insert({
    device_id: data.device_id,
    is_raining: data.isRaining,
    is_dark: data.isDark,
    rain_status: data.rainStatus,
    light_status: data.lightStatus,
    servo_position: data.servoPosition,
    is_roof_open: data.isRoofOpen,
    roof_status: data.roofStatus,
    weather_condition: data.weatherCondition,
    is_safe: data.isSafe,
    mode: data.mode,
    counter: data.counter,
    temperature: data.temperature,
    humidity: data.humidity,
    fuzzy_condition: data.fuzzyCondition,
    dinamo_status: data.dinamoStatus,
    is_buzzer_on: data.isBuzzerOn,
  });

  if (error) {
    console.error("Error saving to Supabase:", error);
    return false;
  }
  return true;
}

// Function to get recent sensor data
export async function getRecentSensorData(limit = 50) {
  const { data, error } = await supabase
    .from("sensor_logs")
    .select("*")
    .order("created_at", { ascending: false })
    .limit(limit);

  if (error) {
    console.error("Error fetching from Supabase:", error);
    return [];
  }
  return data;
}
