"use strict";

(() => {
  const $ = id => document.getElementById(id);
  const format = value => Number.isFinite(value) ? new Intl.NumberFormat("ko-KR", {maximumFractionDigits: 1}).format(value) : "—";
  let latest = null, selectedNpc = null, lastSequence = null, lastSession = null;
  let optionsKey = "", latestReceived = 0;

  function node(tag, text, className) {
    const element = document.createElement(tag);
    if (text !== undefined) element.textContent = text;
    if (className) element.className = className;
    return element;
  }
  function villageName(id) {
    return latest?.villages.find(v => v.id === id)?.name ?? "마을 미소속";
  }
  function health(entity) {
    const wrapper = node("div", undefined, "health");
    wrapper.append(node("span", `${format(entity.health)} / ${format(entity.maxHealth)}`));
    const track = node("div", undefined, "track");
    const ratio = entity.maxHealth > 0 ? Math.min(100, Math.max(0, entity.health / entity.maxHealth * 100)) : 0;
    const fill = node("div", undefined, "fill" + (ratio < 35 ? " low" : ""));
    fill.style.width = `${ratio}%`;
    track.append(fill); wrapper.append(track); return wrapper;
  }
  function state(entity) {
    if (!entity.active) return node("span", entity.alive ? "비활성" : "제압 / 비활성", "state inactive");
    if (entity.recoverySeconds > 0) return node("span", `회복 대기 ${format(entity.recoverySeconds)}초`, "state recovering");
    if (!entity.alive) return node("span", "쓰러짐", "state inactive");
    if (entity.hostile) return node("span", "플레이어 적대", "state hostile");
    return node("span", "활동 중", "state");
  }
  function cell(row, value) {
    const td = node("td");
    td.append(value instanceof Node ? value : node("span", value));
    row.append(td); return td;
  }
  function emptyTable(id, count, text) {
    const row = node("tr"), td = node("td", text, "empty");
    td.colSpan = count; row.append(td); $(id).replaceChildren(row);
  }
  function detail() {
    const entity = latest?.npcs.find(npc => npc.id === selectedNpc);
    $("npc-detail").hidden = !entity;
    if (!entity) return;
    const heading = node("h3", `${entity.name} · ${entity.job}`);
    const weights = entity.weights;
    $("npc-detail").replaceChildren(heading,
      node("p", `소속: ${villageName(entity.villageId)} / Actor ID: ${entity.id} / 목표 Actor: ${entity.targetId === "0" ? "없음" : entity.targetId}`),
      node("p", `성향: 생존 ${format(weights.survival)} / 재산 ${format(weights.wealth)} / 깨달음 ${format(weights.insight)} / 약탈 ${format(weights.raid)}`),
      node("p", `성장 ${format(entity.growth)} / 승리 ${format(entity.victories)} / 약탈 위반 ${entity.offender ? "있음" : "없음"}`),
      node("p", `플레이어 상대 위치: X ${format(entity.position.x)}, Y ${format(entity.position.y)}`));
  }
  function tables() {
    if (!latest) return;
    const village = $("village-filter").value, job = $("job-filter").value;
    const query = $("search").value.trim().toLocaleLowerCase("ko");
    const rows = latest.npcs.filter(npc =>
      (village === "all" || npc.villageId === village) && (job === "all" || npc.job === job)
      && ($("inactive-filter").checked || npc.active)
      && `${npc.name} ${npc.action} ${npc.job}`.toLocaleLowerCase("ko").includes(query));
    $("row-count").textContent = `${rows.length}명 표시 / 전체 ${latest.npcs.length}명`;
    $("npcs").replaceChildren();
    for (const npc of rows) {
      const row = node("tr"), name = node("button", npc.name, "npc-button");
      name.type = "button";
      name.addEventListener("click", () => { selectedNpc = npc.id; detail(); });
      const who = cell(row, name); who.append(node("small", villageName(npc.villageId)));
      cell(row, npc.job); cell(row, npc.action); cell(row, health(npc)); cell(row, `${format(npc.gold)}전`);
      cell(row, format(npc.goods)); cell(row, format(npc.insight)); cell(row, state(npc));
      $("npcs").append(row);
    }
    if (!rows.length) emptyTable("npcs", 8, "조건에 맞는 NPC가 없습니다.");
    $("monsters").replaceChildren();
    const monsters = latest.monsters.filter(mob => village === "all" || mob.villageId === village);
    for (const mob of monsters) {
      const row = node("tr");
      cell(row, (mob.fieldBoss ? "★ 중간 보스 · " : mob.guardian ? "◆ " : "") + mob.name); cell(row, villageName(mob.villageId));
      cell(row, health(mob)); cell(row, state(mob));
      cell(row, `X ${format(mob.position.x)}, Y ${format(mob.position.y)}`);
      $("monsters").append(row);
    }
    if (!monsters.length) emptyTable("monsters", 5, "현재 등록된 몬스터가 없습니다.");
    detail();
  }
  function filters() {
    const jobs = [...new Set(latest.npcs.map(npc => npc.job))].sort();
    const key = JSON.stringify([latest.villages.map(v => [v.id, v.name]), jobs]);
    if (key === optionsKey) return;
    optionsKey = key;
    const oldVillage = $("village-filter").value, oldJob = $("job-filter").value;
    $("village-filter").replaceChildren(new Option("전체 마을", "all"), ...latest.villages.map(v => new Option(v.name, v.id)));
    $("job-filter").replaceChildren(new Option("전체 직업", "all"), ...jobs.map(job => new Option(job, job)));
    $("village-filter").value = latest.villages.some(v => v.id === oldVillage) ? oldVillage : "all";
    $("job-filter").value = jobs.includes(oldJob) ? oldJob : "all";
  }
  function villageCards() {
    $("villages").replaceChildren();
    for (const village of latest.villages) {
      const card = node("article", undefined, "village"), heading = node("header");
      heading.append(node("h3", village.name), node("span", village.active ? "운영 중" : "비활성", "badge"));
      const dl = node("dl");
      for (const [label, value] of [
        ["인간 주민 / 활동 중", `${village.counts.npcs} / ${village.counts.npcsAlive}`],
        ["몬스터 / 생존", `${village.counts.monsters} / ${village.counts.monstersAlive}`],
        ["주민 자산", `${format(village.totalGold)}전`], ["주민 평균 체력", `${format(village.averageHealthPercent)}%`],
        ["잔향석", format(village.totalGoods)], ["깨달음 합계", format(village.totalInsight)]]) {
        const item = node("div"); item.append(node("dt", label), node("dd", value)); dl.append(item);
      }
      const chips = node("div", undefined, "action-chips");
      for (const action of village.actions) chips.append(node("span", `${action.name} ${action.count}`));
      card.append(heading, dl, chips);
      card.append(node("p", village.activity
        ? `누적 거래 ${village.activity.trades} / 사냥 ${village.activity.hunts} / 약탈 ${village.activity.raids}`
        : "이 마을의 누적 활동 통계는 아직 수집되지 않았습니다."));
      $("villages").append(card);
    }
    if (!latest.villages.length) $("villages").append(node("div", "마을이 없는 파밍 모드입니다.", "empty"));
  }
  function incidentTable() {
    const filter = $("incident-filter").value;
    const rows = (latest?.incidents ?? []).filter(event => filter === "all" || event.type === filter);
    $("incidents").replaceChildren();
    for (const event of rows) {
      const row = node("tr");
      cell(row, `#${event.id} / ${format(event.simulationSeconds)}초`);
      cell(row, event.type === "murder" ? "살인 · 발생 확인" : "소문 · 미검증");
      cell(row, villageName(event.villageId));
      cell(row, `${event.actorName} → ${event.victimName}${event.witnessName ? ` / 목격: ${event.witnessName}` : " / 목격자 없음"}`);
      cell(row, event.text + (event.sourceId !== "0" ? ` / 원본 #${event.sourceId}` : ""));
      $("incidents").append(row);
    }
    if (!rows.length) emptyTable("incidents", 5, "조건에 맞는 사건이 없습니다. 사건 발생 시 자동 표시됩니다.");
    $("incident-totals").textContent = `월드 누적 살인 ${latest?.incidentTotals?.murder ?? 0} · 소문 ${latest?.incidentTotals?.rumor ?? 0} / 보관 ${latest?.incidents?.length ?? 0}건`;
  }
  function render(data) {
    if (data.sessionId !== lastSession || data.worldEpoch !== latest?.worldEpoch) selectedNpc = null;
    latest = data;
    lastSession = data.sessionId;
    lastSequence = data.sequence;
    const counts = data.counts;
    $("npc-alive").textContent = format(counts.npcsAlive);
    $("npc-total").textContent = `등록 ${counts.npcs} / 회복 ${counts.npcsRecovering} / 비활성 ${counts.npcsInactive}`;
    $("mob-alive").textContent = format(counts.monstersAlive);
    $("mob-total").textContent = `등록 ${counts.monsters} / 재출현 대기 ${counts.monstersRecovering} / 비활성 ${counts.monstersInactive}`;
    $("village-count").textContent = format(data.villages.length);
    $("chunks").textContent = `스트리밍 청크 ${data.residentChunks} / Actor ${data.actorCount}`;
    $("wealth").textContent = `${format(data.economy.npcGold)}전`;
    $("inventory").textContent = `잔향석 ${format(data.economy.npcGoods)} / 깨달음 ${format(data.economy.npcInsight)}`;
    $("player-position").textContent = `청크 ${data.player.chunkX}, ${data.player.chunkY} / ${format(data.player.localX)}, ${format(data.player.localY)}`;
    $("quest").textContent = data.mode === "living-world" ? data.quest.objective : "파밍 / 보스 모드";
    $("sim-time").textContent = data.mode === "living-world" ? `${format(data.simulationSeconds)}초` : "생활 시뮬레이션 비활성";
    $("message-id").textContent = `#${data.sequence} · 세션 ${data.worldEpoch}`;
    $("events").replaceChildren(...data.events.map(event => node("li", event)));
    if (!data.events.length) $("events").append(node("li", "아직 기록된 게임 이벤트가 없습니다."));
    $("download").disabled = false;
    if ($("json-panel").open) $("json").textContent = JSON.stringify(data, null, 2);
    filters(); villageCards(); tables(); incidentTable();
  }
  function connection(text, style, message) {
    $("connection").textContent = text;
    $("connection").className = `badge ${style}`;
    $("notice").textContent = message;
    $("notice").className = "notice" + (style === "offline" || style === "waiting" ? " stale" : "");
  }
  async function poll() {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 2000);
    try {
      const response = await fetch("/api/state", {cache: "no-store", signal: controller.signal});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      if (data.type === "waiting") {
        connection("첫 상태 대기", "waiting", "로컬 서버에 연결했습니다. 게임 상태 생성을 기다립니다.");
      } else {
        if (data.type !== "world_snapshot" || data.schemaVersion !== 1 || !Array.isArray(data.npcs) || !Array.isArray(data.monsters) || !Array.isArray(data.villages)) throw new Error("지원하지 않는 JSON 메시지");
        if (data.sequence !== lastSequence || data.sessionId !== lastSession) {
          latestReceived = Date.now(); render(data);
        }
        const age = Date.now() - data.generatedAtUnixMs;
        connection(age > 3000 ? "갱신 지연" : "실시간 연결", age > 3000 ? "waiting" : "",
          age > 3000 ? "게임 상태가 3초 이상 갱신되지 않았습니다. 마지막 상태를 표시합니다."
                     : "게임에서 JSON 상태를 수신 중입니다. 화면 밖 NPC도 포함하며 0.5초마다 확인합니다.");
      }
    } catch (error) {
      connection("연결 끊김", "offline", `게임 연결을 기다립니다. 게임 실행 여부와 모니터 포트를 확인하세요. ${latest ? "마지막 수신 상태를 유지합니다." : ""} (${error.message})`);
    } finally {
      clearTimeout(timeout);
      setTimeout(poll, 500);
    }
  }
  ["village-filter", "job-filter", "search", "inactive-filter"].forEach(id => $(id).addEventListener("input", tables));
  $("incident-filter").addEventListener("input", incidentTable);
  $("json-panel").addEventListener("toggle", () => { if (latest && $("json-panel").open) $("json").textContent = JSON.stringify(latest, null, 2); });
  $("download").addEventListener("click", () => {
    if (!latest) return;
    const url = URL.createObjectURL(new Blob([JSON.stringify(latest, null, 2)], {type: "application/json"}));
    const link = node("a"); link.href = url; link.download = `world-${latest.sequence}.json`; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  setInterval(() => {
    if (latestReceived) $("updated").textContent = `마지막 수신 ${new Date(latestReceived).toLocaleTimeString("ko-KR")} · ${Math.max(0, Math.floor((Date.now() - latestReceived) / 1000))}초 전`;
  }, 1000);
  if (location.protocol === "file:") {
    connection("로컬 서버로 열기", "waiting", "게임을 실행한 뒤 http://127.0.0.1:8765/ 에 접속하세요. HTML은 게임의 로컬 서버를 통해 열어야 합니다.");
    const link = node("a", "로컬 대시보드 열기"); link.href = "http://127.0.0.1:8765/"; $("notice").append(" ", link);
  } else poll();
})();
