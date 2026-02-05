<!-- SafeSave Fab Description (Symbol Icons) -->
<h2>SafeSave — Source Control Safety for Unreal Engine</h2>
<p>SafeSave is a compact UE5 editor toolbar that keeps your source control status and unsaved assets impossible to ignore. It reduces risky syncs, prevents lost work, and keeps teams aligned with clear, always‑on signals.</p>

<h3>USP & Benefits</h3>
<ul>
  <li>🛡️ <strong>Always‑On Safety Signals</strong> — The toolbar label shows <em>branch/workspace | state</em> at a glance.</li>
  <li>👁️ <strong>Clear Risk Visibility</strong> — Conflicts, Diverged, Behind, Changes, Unsaved, and Login Required are color‑coded.</li>
  <li>🧭 <strong>Provider‑Aware Actions</strong> — Git: Fetch / Pull (Rebase) / Push. Plastic: Update Workspace.</li>
  <li>🔒 <strong>Safety Gates</strong> — Pull/Push/Update are disabled unless the workspace is clean and assets are saved.</li>
  <li>🔁 <strong>Auto‑Fetch (Git‑Only)</strong> — Optional background fetch keeps refs fresh without touching local files.</li>
  <li>🔔 <strong>Toast Feedback</strong> — Optional status‑change toasts keep you informed in full‑screen viewports.</li>
</ul>

<h3>What You Get</h3>
<ul>
  <li>✅ <strong>Git + Plastic SCM Support</strong> — Uses the active UE Source Control provider.</li>
  <li>✅ <strong>Non‑Intrusive Workflow</strong> — No forced staging or commits. SafeSave surfaces risk only.</li>
  <li>✅ <strong>Fast Polling Controls</strong> — Tune refresh intervals in Editor Preferences.</li>
</ul>

<h3>Requirements</h3>
<ul>
  <li>🧰 <strong>Git:</strong> <code>git.exe</code> on PATH.</li>
  <li>🔑 <strong>Plastic SCM:</strong> <code>cm</code> CLI installed and logged in via UE Source Control.</li>
</ul>

<p><strong>SafeSave keeps your pipeline safe, visible, and low‑risk — without changing how you already work.</strong></p>

Code by Andras Gregori @ https://gregorigin.com/

---

Legacy Description follows (v0.1 - 0.5, 2025 version):


🛑 STOP SCREAMING "WHO LOCKED THE MAP?!"

Merge conflicts kill momentum.
Unreal Engine’s native Source Control integration is powerful, but "silent." It lets you edit files that are stale. It lets you work on maps that your teammates have locked. By the time you try to save, it’s too late—work is lost.

SafeSave changes the paradigm. It is an Air Traffic Controller that lives next to your Play button. It creates a direct, visual link between your local work and the server status, warning you of danger before you make a mistake.

🚦 THE TRAFFIC LIGHT SYSTEM

SafeSave replaces complex context menus with a single, intelligent status indicator:

    🟢 Green (Synced): You are safe. Your files match the server perfectly.

    🔵 Blue (Update Available): The "Killer" Feature. SafeSave effectively "looks into the future," detecting that a teammate has pushed code while you were working. Click to Pull/Sync instantly.

    🟠 Orange (Push Changes): You have unsaved work. Click to Save & Submit without opening four different windows.

    🔴 Red (Conflict Imminent): DANGER. You have local changes AND the server has updates. The button triggers a Safe-Update Protocol (Force Save -> Sync -> Native Merge) to protect your data.

🔒 INSTANT LOCK DETECTION (Billy Mode)

If you use Perforce (P4) or Git LFS Locking:
SafeSave scans the lock status of assets the moment you open them. If a file is checked out by another user, you get an immediate, non-blocking Toast Notification:

    "LOCKED by Teammate: [Username]"

No more wasted hours working on a file you can't save.

🛠️ TECHNICAL ARCHITECTURE

SafeSave is engineered for high-performance production environments. It is not a Blueprint widget; it is a native C++ Editor Module.

    Zero-Tick Overhead: Uses Slate Active Timers (1Hz polling) instead of Tick(). It costs 0.00ms on the Game Thread when idle.

    Deep Memory Scanning: Uses TObjectIterator to detect dirty states in RAM, offering faster feedback than disk scanning.

    Ghost Filtering: Aggressively ignores /Temp/, /Engine/, and World Partition InstanceOf artifacts to prevent false positives.

    Native UI injection: Integrates seamlessly into UToolMenus.

🔌 COMPATIBILITY

SafeSave acts as a HUD (Heads-Up Display) for standard Unreal Engine Source Control. It works with:

    Perforce (P4)

    Git (Beta & Experimental)

    PlasticSCM (Unity DevOps)

    Anchorpoint (Compatible workflow)

    Diversion

📦 WHAT YOU GET

    The SafeSave Editor Plugin (Win64).

    SafeSave Professional: Lifetime License for commercial use.

    SafeSave Personal: License for individual creators.
