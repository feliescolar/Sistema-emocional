import { serve } from "https://deno.land/std@0.168.0/http/server.ts"
import { createClient } from 'https://esm.sh/@supabase/supabase-js@2'
import fuzzy from 'npm:fuzzylogic'

console.log("Cerebro v4.1 (Lógica Difusa Total + Auto-Guardado) Activado 🧠🛡️");

function evaluarRiesgoConLibreria(bpm: number, spo2: number, cargaEmocional: number) {
    // 1. Fuzzificación

    // Pulsaciones

    const bpmNormal = fuzzy.trapezoid(bpm, 0, 0, 85, 105);
    const bpmAlto = fuzzy.trapezoid(bpm, 85, 105, 115, 125);
    const bpmMuyAlto = fuzzy.trapezoid(bpm, 110, 125, 200, 200);

    // Oxígeno

    const oxigenoCritico = fuzzy.trapezoid(spo2, 0, 0, 88, 92);
    const oxigenoBajo = fuzzy.trapezoid(spo2, 88, 92, 94, 96);
    const oxigenoNormal = fuzzy.trapezoid(spo2, 94, 96, 100, 100);

    // Carga Emocional 

    const emoEstable = fuzzy.trapezoid(cargaEmocional, 0, 0, 30, 50);
    const emoVulnerable = fuzzy.trapezoid(cargaEmocional, 30, 50, 70, 85);
    const emoCrisis = fuzzy.trapezoid(cargaEmocional, 70, 85, 100, 100);

    // 2. Reglas de inferencia

    let pesoTotal = 0;
    let sumaRiesgo = 0;

    const agregarRegla = (activacion: number, nivelRiesgo: number) => {
        if (activacion > 0) {
            sumaRiesgo += activacion * nivelRiesgo;
            pesoTotal += activacion;
        }
    };

    // Reglas de oxígeno

    agregarRegla(oxigenoCritico, 95); 
    agregarRegla(oxigenoBajo, 75);    

    // Reglas de crisis

    agregarRegla(Math.min(bpmMuyAlto, emoCrisis), 90); 
    agregarRegla(Math.min(bpmAlto, emoCrisis), 85);    
    agregarRegla(Math.min(bpmNormal, emoCrisis), 80);  

    // Reglas de vulnerabilidad

    agregarRegla(Math.min(bpmAlto, emoVulnerable), 60);   
    agregarRegla(Math.min(bpmNormal, emoVulnerable), 40);  

    // Reglas de estabilidad

    agregarRegla(Math.min(bpmMuyAlto, emoEstable), 25); 
    agregarRegla(Math.min(bpmAlto, emoEstable), 15);   
    agregarRegla(Math.min(bpmNormal, oxigenoNormal, emoEstable), 10); 

    // 3. Defuzzificación 

    if (pesoTotal === 0) return 0; 
    return Math.round(sumaRiesgo / pesoTotal);
}

serve(async (req) => {
  try {
   
    const payload = await req.json();
    const record = payload.record;

if (
    record.bpm === null || record.bpm === 0 || 
    record.spo2 === null || record.spo2 === 0 || 
    record.valor_riesgo !== null
) {
  return new Response(
      JSON.stringify({ status: "skipped", mensaje: "Faltan datos vitales o ya está calculado" }), 
      { headers: { "Content-Type": "application/json" } }
  );
}

    // Cálculo de la carga emocional

    let calculoBase = 
        (record.total_ira * 1.0) + 
        (record.total_miedo * 1.0) + 
        (record.total_tristeza * 0.6) + 
        (record.total_asco * 0.4) + 
        (record.total_sorpresa * 0.2) - 
        (record.total_alegria * 0.8);

    let cargaEmocional = Math.max(0, Math.min(100, calculoBase));

    // Lógica difusa

    const nivelRiesgo = evaluarRiesgoConLibreria(record.bpm, record.spo2, cargaEmocional);
    
    const riesgoFinal = nivelRiesgo / 100;

    console.log(`BPM: ${record.bpm} | SpO2: ${record.spo2} | Carga Emo: ${cargaEmocional.toFixed(2)} -> RIESGO FINAL: ${riesgoFinal}`);

    // Guardar resultado 
    
    const supabase = createClient(
      Deno.env.get('SUPABASE_URL') ?? '',
      Deno.env.get('SUPABASE_SERVICE_ROLE_KEY') ?? ''
    );

    const { error } = await supabase
      .from('evaluaciones')
      .update({ valor_riesgo: riesgoFinal }) 
      .eq('id', record.id);

    if (error) throw error;

    return new Response(
      JSON.stringify({ mensaje: "Riesgo actualizado automáticamente", riesgo: riesgoFinal }),
      { headers: { "Content-Type": "application/json" } }
    );

  } catch (error) {
    return new Response(JSON.stringify({ error: error.message }), {
      status: 400,
      headers: { "Content-Type": "application/json" }
    });
  }
})