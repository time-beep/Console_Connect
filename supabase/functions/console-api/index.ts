import { createClient } from "npm:@supabase/supabase-js@2";

const cors = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

const json = (status: number, data: unknown) => new Response(JSON.stringify(data), {
  status,
  headers: { "Content-Type": "application/json", "Cache-Control": "no-store", ...cors },
});

const allowedGames = new Set(["snake", "pong", "space_invaders", "pacman", "breakout", "tetris"]);
const allowedCrewNames = new Set([
  "RedToaster", "EggRice", "MoonSpoon", "ToastWizard", "PickleRocket",
  "NoodleGoblin", "CloudPotato", "TurboMango", "WaffleNinja", "CheeseComet",
  "SockDragon", "PastaWizard", "DiscoTurnip", "PixelPickle", "BananaHelmet",
  "CactusPancake", "JellyBoots", "CosmicToast", "MangoBucket", "FuzzyLadder",
  "SoupPirate", "MarbleDuck", "GrapeEngine", "SleepyFork", "RobotMuffin",
  "TinyVolcano", "PillowKnight", "SpicyTeacup", "LaserCarrot", "DoodleTurtle",
  "CrankyDonut", "BubbleHammer", "KiwiRocket", "VelvetPotato", "WobbleCheese",
  "FriedPlanet", "SodaGoblin", "CrispyMoon", "BiscuitNinja", "RubberWizard",
  "PickleSocks", "CosmicBucket", "PuddingPilot", "SneakyWaffle", "MossyRobot",
  "TacoLantern", "GummyCompass", "RogueMuffin", "MarshmallowJet", "OnionSatellite",
]);

Deno.serve(async (req: Request) => {
  if (req.method === "OPTIONS") return new Response(null, { status: 204, headers: cors });
  if (req.method !== "POST") return json(405, { ok: false, error: "Use POST" });

  let body: Record<string, unknown>;
  try { body = await req.json(); }
  catch { return json(400, { ok: false, error: "Invalid JSON" }); }

  const url = Deno.env.get("SUPABASE_URL");
  const serviceKey = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY");
  if (!url || !serviceKey) return json(500, { ok: false, error: "Server configuration error" });
  const db = createClient(url, serviceKey, { auth: { autoRefreshToken: false, persistSession: false } });

  if (body.action === "dashboard") {
    const { data: users, error: usersError } = await db.from("console_users").select("id, user_id").order("user_id");
    if (usersError) return json(500, { ok: false, error: "Could not load users" });
    const { data: scoreRows, error: scoresError } = await db.from("high_scores").select("user_id, game, high_score");
    if (scoresError) return json(500, { ok: false, error: "Could not load scores" });
    const scoresByUser = new Map<number, Record<string, number>>();
    for (const row of scoreRows ?? []) {
      const scores = scoresByUser.get(row.user_id) ?? {};
      scores[row.game] = row.high_score;
      scoresByUser.set(row.user_id, scores);
    }
    return json(200, { ok: true, users: (users ?? []).map((u) => ({ user_id: u.user_id, scores: scoresByUser.get(u.id) ?? {} })) });
  }

  if (body.action === "list_users") {
    const { data, error } = await db.from("console_users").select("user_id").order("user_id");
    if (error) return json(500, { ok: false, error: "Could not load users" });
    return json(200, { ok: true, users: (data ?? []).map((row) => row.user_id) });
  }

  const userId = typeof body.user_id === "string" ? body.user_id.trim() : "";
  const password = typeof body.password === "string" ? body.password : "";
  if (!userId || !/^[UDLR]{4}$/.test(password)) return json(400, { ok: false, error: "Invalid user ID or password format" });

  if (body.action === "create_user") {
    if (!allowedCrewNames.has(userId)) return json(400, { ok: false, error: "Username is not in the crew name list" });
    const { data: existing, error: lookupError } = await db.from("console_users").select("id").eq("user_id", userId).maybeSingle();
    if (lookupError) return json(500, { ok: false, error: "Could not check username" });
    if (existing) return json(409, { ok: false, error: "Name already taken" });
    const { error: insertError } = await db.from("console_users").insert({ user_id: userId, password });
    if (insertError) {
      if (insertError.code === "23505") return json(409, { ok: false, error: "Name already taken" });
      return json(500, { ok: false, error: "Could not create user" });
    }
    return json(201, { ok: true, user_id: userId, scores: {} });
  }

  const { data: user, error: userError } = await db.from("console_users").select("id, user_id, password").eq("user_id", userId).maybeSingle();
  if (userError) return json(500, { ok: false, error: "Login service error" });
  if (!user || user.password !== password) return json(401, { ok: false, error: "Wrong user ID or password" });

  if (body.action === "login") {
    const { data, error } = await db.from("high_scores").select("game, high_score").eq("user_id", user.id);
    if (error) return json(500, { ok: false, error: "Could not load high scores" });
    const scores: Record<string, number> = {};
    for (const row of data ?? []) scores[row.game] = row.high_score;
    return json(200, { ok: true, user_id: user.user_id, scores });
  }

  if (body.action === "update_score") {
    const game = typeof body.game === "string" ? body.game : "";
    const score = body.score;
    if (!allowedGames.has(game) || !Number.isInteger(score) || (score as number) < 0 || (score as number) > 2147483647) {
      return json(400, { ok: false, error: "Invalid game or score" });
    }
    const { data: current, error: readError } = await db.from("high_scores").select("high_score").eq("user_id", user.id).eq("game", game).maybeSingle();
    if (readError) return json(500, { ok: false, error: "Could not read score" });
    const best = Math.max(current?.high_score ?? 0, score as number);
    if (!current || best > current.high_score) {
      const { error } = await db.from("high_scores").upsert(
        { user_id: user.id, game, high_score: best, updated_at: new Date().toISOString() },
        { onConflict: "user_id,game" },
      );
      if (error) return json(500, { ok: false, error: "Could not save score" });
    }
    return json(200, { ok: true, game, high_score: best });
  }
  return json(400, { ok: false, error: "Unknown action" });
});
