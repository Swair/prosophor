# Whitepaper: Plugin-Based Proactive Multimodal Interaction Trigger Architecture

> **Practice Foundation**: This architecture is based on the engineering practice of the open-source project **AiCode** (C++ intelligent coding assistant CLI), and has been core implemented in that project.
>
> **Project URL**: https://github.com/Swair/claude-code-cc-version

---

## 1. Abstract

The development of intelligent Agents has gone through four key stages:

| Stage | Time | Characteristics |
|-------|------|-----------------|
| Early Exploration | 1956—1995 | Concept embodiment; Microsoft Bob, Clippy and other products attempted human-computer interaction practices |
| Differentiation | 1996—2011 | Product typification; software Agent concepts emerged but were limited by hardware and algorithms |
| Engineering Exploration | 2012—2022 | Deep learning breakthroughs; Task/Planning architecture emerged; Agent engineering began exploration |
| Standardization | 2023—Present | LLM-driven Agent explosion; but proactive interaction architecture remains immature |

**Core Problem**: Existing Agent designs universally have **architectural defects**—trigger logic is deeply coupled with the kernel, lacks proactive trigger mechanisms, plugin extensions require source code modifications, and no ecosystem loop support. These issues lead to Agent functionality solidification, iteration difficulties, and inability to support large-scale proactive interaction scenarios.

**This Paper's Innovation**: Proposes a **Plugin-Based Proactive Multimodal Interaction Trigger Architecture**, with core innovations in:
1. **Plugin Framework**: Trigger logic is completely separated from the scheduling kernel, supporting plugin hot-swapping and zero-code extension
2. **Proactive Interaction**: The industry's first proactive interaction trigger framework, achieving large-scale interaction with "proactive prediction and graded low disturbance" through three-mode graded triggering
3. **Ecosystem Loop**: The first plugin ecosystem for proactive interaction scenarios—supporting an engineering system of "development→review→distribution→feedback→iteration" (distinct from existing passive tool-type plugin ecosystems like Skills)

**Engineering Practice Foundation**: This architecture has been implemented and validated in the **AiCode** project—a C++-based intelligent coding assistant CLI tool using the REACT paradigm (Think-Act-Observe loop), supporting Anthropic/Qwen/Ollama multi-LLM providers.

This architecture does not rely on LLM technology breakthroughs, but solves the **engineering implementation and large-scale extension** of Agent proactive interaction at the **architecture design level**.

**Core Innovation Focus**:
- **Proactive Interaction** is the core value proposition
- **Three-Mode Graded Triggering** is the technical means to achieve proactive interaction (periodic/idle/idle_once)

---

## 2. Background and Pain Points

### 2.1 Core Defects in Traditional Agent Design

Current mainstream Agent designs have the following **architectural-level problems** that constrain the implementation of proactive interaction capabilities:

| Defect | Specific Manifestation | Consequences |
|--------|----------------------|--------------|
| **Passive Response Architecture** | Agent Loop triggers after waiting for user commands | Unable to actively perceive scene changes and predict user needs |
| **Trigger Logic Coupled with Kernel** | Trigger conditions are hard-coded in the kernel | Adding/modifying trigger rules requires source code modification and recompilation |
| **No Event Grading Mechanism** | All events use the same trigger logic | Unable to balance emergency response and user do-not-disturb needs |
| **Lack of Plugin Support** | Does not support multi-script formats, high development threshold | Difficulty in functional extension, low plugin reusability |
| **Lack of Ecosystem Loop** | No unified review, distribution, and feedback channels | Plugins are developed in isolation, unable to form iterative optimization |
| **No Proactive Trigger Capability** | Agent Loop only supports passive calls | Unable to achieve "predict needs, actively respond" interaction mode |

> **Key Insight**: The root cause of the above problems does not lie in insufficient technical capabilities, but in **architectural design defects**—trigger logic coupling with the kernel, passive response architecture, and lack of ecosystem support.

### 2.2 Design Goals of This Architecture

Addressing the architectural defects of traditional Agents, the design goals of this architecture are as follows:

| Goal | Traditional Agent Defect Addressed |
|------|-----------------------------------|
| **Architecture Decoupling** | Trigger logic coupled with kernel, extension requires source code modification |
| **Proactive Triggering** | Passive response architecture, unable to predict user needs |
| **Graded Interaction** | No event grading, unable to balance response and do-not-disturb |
| **Flexible Invocation** | Lack of plugin support, high development threshold |
| **Ecosystem Loop** | No unified ecosystem support, unable to iterate and optimize |

---

## 3. Related Work

### 3.1 Development of Passive Response Agents

| Stage | Representative Products/Frameworks | Trigger Mode | Limitations |
|-------|-----------------------------------|--------------|-------------|
| Early Chatbots (1966-2010) | ELIZA, Alice | User input → Rule matching → Response | No proactive interaction capability |
| Intelligent Assistants (2011-2022) | Siri, Alexa, Xiao Ai | Wake word → Speech recognition → Execution | Requires user wake-up, unable to predict |
| LLM Agents (2023—Present) | ChatGPT, Claude, OpenAI Assistants API | User command → Agent Loop → Tool invocation | **Agent Loop only supports passive calls**, no proactive trigger mechanism |
| Plugin Ecosystem (2023—Present) | OpenAI Skills, Claude MCP | User/Agent actively invokes plugins | **Passive tool-type plugins**, requires waiting for invocation |

> **Key Insight**: The trigger mechanisms of existing Agent architectures (including LLM Agents) are all **passive response**—waiting for user commands to start the Agent Loop.

### 3.2 Rule Engine-Based Proactive Interaction Tools

| Product | Domain | Trigger Mode | Limitations |
|---------|--------|--------------|-------------|
| Hazel (macOS) | File Automation | Listen to file changes → Execute preset actions | Fixed rules, cannot extend, no intelligent response |
| AutoHotkey (Windows) | Desktop Automation | Hotkey/Event → Execute script | Script bound to configuration, no plugin ecosystem |
| IFTTT / Zapier | Web Service Automation | Webhook/Scheduled → Invoke API | Cloud rule engine, not local Agent architecture |
| Home Assistant | Smart Home | Sensor status → Linkage devices | Closed rule configuration, not plugin-based |

> **Key Insight**: These tools implement **event-based proactive triggering**, but use a **fixed rule engine** mode, cannot extend through plugins, and lack LLM linkage capabilities.

### 3.3 Proactive Interaction Research Exploration

| Research Direction | Representative Work | Core Idea | Limitations |
|-------------------|---------------------|-----------|-------------|
| Proactive AI Assistant | Microsoft Cortana (2014-2020) | Time/location-based proactive reminders | Rule-driven, no architectural-level support |
| Context-Aware Agent | Academia (e.g., Context-Aware Computing) | Perceive environmental context, adaptive response | Research prototype, no engineering implementation |
| Ambient Intelligence | EU Research Projects | Ambient intelligence, imperceptible interaction | Concept ahead of time, insufficient engineering |

### 3.4 Positioning of This Architecture

| Dimension | Traditional Agent | Rule Engine Tools | This Architecture |
|-----------|------------------|-------------------|-------------------|
| Trigger Mode | Passive response (user command) | Fixed rules (if-then) | **Plugin-based proactive trigger** |
| Extension Capability | Modify kernel/configuration | Modify script/rules | **Plugin hot-swapping** |
| Response Mode | LLM generation | Execute preset actions | **LLM scenario-based response** |
| Ecosystem Support | Skills/MCP (passive) | None | **Proactive interaction plugin community** |

**Relationship Between This Architecture and AiCode Project**:
- **AiCode**: C++-based intelligent coding assistant CLI tool using REACT paradigm (Think-Act-Observe loop)
- **This Architecture**: Proactive interaction subsystem of AiCode, embedded as a core module
- **Architecture Reusability**: Although derived from AiCode practice, designed as a generic architecture that can be independently integrated into other Agent frameworks

> **Key Insight**: This architecture is not pure theoretical research, but an **implementable solution derived from engineering practice and validated by projects**.

---

## 4. Core Design Philosophy

With **"architecture decoupling as the core, proactive triggering as the breakthrough, graded low disturbance as the support, and ecosystem loop as the guarantee"**, reconstruct the Agent proactive interaction trigger system through architectural-level innovation.

**Core Philosophy Comparison**:

| Dimension | Traditional Agent Design | This Architecture Design |
|-----------|------------------------|-------------------------|
| Architecture Mode | Monolithic coupling | Three-layer decoupling |
| Trigger Mode | Passive response (user command-driven) | Proactive prediction (event/state-driven) |
| Extension Mode | Modify source code + recompile | Plugin hot-swapping + zero kernel modification |
| Event Processing | Unified processing, no grading | Three-mode grading (periodic/idle/idle_once) |
| Ecosystem Support | None/scattered | Plugin community loop |
| Iteration Capability | Depends on version updates | Plugin autonomous iteration + feedback loop |

---

## 5. Overall Architecture Design

This architecture is divided into two parts: **runtime architecture** and **ecosystem support system**:
- **Runtime Architecture** (two-layer innovation + one-layer reuse):
  - **Plugin Layer** (Innovation): Trigger logic decoupled from the kernel, supporting hot-swapping
  - **Scheduling Layer** (Innovation): Three-mode graded triggering, active perception and scheduling
  - **Execution Layer** (Reuse): Reuses existing Agent framework tool execution capabilities (ReAct Act part)
- **Ecosystem Support System**: Plugin community (server-side + client-side)—independent infrastructure

### 5.1 Architecture Comparison: Traditional Agent vs This Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                Traditional Agent Architecture (Coupled)       │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Kernel Layer: Trigger + Scheduling + Execution       │   │
│  │  Plugins: Fixed functionality, extension requires     │   │
│  │             kernel source modification               │   │
│  └─────────────────────────────────────────────────────┘   │
│  → Problems: Difficult extension, high maintenance cost,    │
│              unable to proactively trigger                  │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│           Plugin Community (Ecosystem, Independent)          │
│           Plugin Upload, Security Check, Review,            │
│           Distribution, Update                              │
└─────────────────────────────────────────────────────────────┘
                            │ Download/Update
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                  This Runtime Architecture (Three Layers)    │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Plugin Layer (Innovation): Trigger logic decoupled  │   │
│  │     active/ directory, trigger + trigger_mode.cfg    │   │
│  │     → All trigger logic sinks to plugins, zero       │   │
│  │       coupling with scheduling kernel                │   │
│  └─────────────────────────────────────────────────────┘   │
│                            │                                │
│                            ▼                                │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Scheduling Layer (Innovation): Three-mode graded     │   │
│  │     triggering, active perception & scheduling       │   │
│  │     Plugin scan, classified invocation, priority     │   │
│  │     scheduling, LLM linkage                          │   │
│  │     → Only responsible for scheduling, does not      │   │
│  │       contain specific trigger logic                 │   │
│  └─────────────────────────────────────────────────────┘   │
│                            │                                │
│                            ▼                                │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Execution Layer (Reuse): Reuses Agent framework     │   │
│  │     tool execution capabilities                      │   │
│  │     Scenario interaction, multimodal output,         │   │
│  │     configurable (ReAct Act)                         │   │
│  └─────────────────────────────────────────────────────┘   │
│  → Innovation Focus: Plugin Layer + Scheduling Layer,      │
│    Execution Layer reuses existing framework               │
└─────────────────────────────────────────────────────────────┘
```

### 5.2 Ecosystem Support System: Plugin Community

The plugin community is an infrastructure independent of the runtime architecture, providing ecosystem support for the architecture:

| Component | Function |
|-----------|----------|
| **Server-side** | Plugin upload, security check, manual review, classified storage, version management, statistics |
| **Client-side** (embedded in runtime) | Plugin search, one-click download, automatic deployment, version update, feedback submission |
| **Value** | Build a complete ecosystem loop of "development→review→distribution→use→feedback→iteration" |

**Differentiated Positioning**:

| Ecosystem Type | Representative Products | Service Objects | Plugin Type |
|---------------|------------------------|-----------------|-------------|
| General Plugin Ecosystem | OpenAI Skills, Claude MCP | Passive Response Agents | Passive tool-type plugins |
| **Proactive Interaction Plugin Ecosystem** | This Plugin Community | **Proactive Interaction Agents** | **Trigger rule-type plugins** |

> **Note**: There are already general plugin ecosystems (such as Skills, MCP), but they are **passive tool-type** plugins that require waiting for user or Agent invocation. This plugin community focuses on **proactive trigger rule-type** plugins, filling the gap in the proactive interaction field.

### 5.3 Runtime Architecture Layers Description

#### 5.3.1 Plugin Layer (Business Logic Layer)

**Traditional Agent Problem**: Trigger logic is hard-coded in the kernel, extension requires source code modification.

**This Innovation**:
- **Storage Directory**: Global unified root directory `active/`, all plugins are independent subdirectories under this directory
- **Plugin Composition**: Each plugin must contain 3 core files:
  - `trigger` (trigger script, any language, returns true/false)
  - `trigger_mode.cfg` (mode configuration file: periodic/idle/idle_once)
  - `ACTIVE.md` (interaction configuration document for LLM linkage)

**Plugin Example** (CPU Temperature Monitoring Plugin):

```
active/
└── cpu_temperature_monitor/
    ├── trigger          # Trigger script (any language, returns true/false)
    ├── trigger_mode.cfg # Mode configuration
    └── ACTIVE.md        # Interaction configuration
```

**trigger Trigger Logic Flow**:

```
┌─────────────────────────────────────────────────────────────┐
│  trigger Execution Flow                                      │
├─────────────────────────────────────────────────────────────┤
│  1. Execute detection logic (e.g., read CPU temperature)     │
│          │                                                  │
│          ▼                                                  │
│  2. Judge whether trigger condition is met                  │
│          │                                                  │
│     ┌────┴────┐                                             │
│     │ Yes     │ No                                          │
│     ▼         ▼                                             │
│  Return true  Return false                                  │
│  (triggered)  (not triggered)                               │
│          │                                                  │
│          ▼                                                  │
│  3. Output trigger reason to stdout (optional)              │
│     Example: "CPU temperature 90°C, exceeds threshold 85°C" │
└─────────────────────────────────────────────────────────────┘
```

**Output Specification**:
- Return `true` = triggered (can print trigger reason to stdout)
- Return `false` = not triggered
- Timeout `100ms` forced termination to avoid blocking scheduling

**trigger_mode.cfg Configuration**:

| Field | Description | Example Value |
|-------|-------------|---------------|
| mode | Trigger mode | periodic / idle / idle_once |
| interval | Execution interval (seconds) | 60 |
| priority | Priority | high / low |

**ACTIVE.md Configuration**:

| Field | Description |
|-------|-------------|
| Trigger Scenario | Describe trigger conditions and event type |
| Interaction Script | Reference template for LLM to generate callback |
| Executable Operations | List of tasks users can execute with one click |

**Post-Trigger Flow**:

```
trigger returns true → Read ACTIVE.md → Send to LLM → Generate response → Notify user/Execute task
```

- **Plugin Characteristics**: Supports hot-swapping, adding/modifying/uninstalling plugins does not require restarting the architecture or modifying kernel source code

#### 5.3.2 Scheduling Layer (Core Hub Layer)

**Traditional Agent Problem**: Scheduling logic is coupled with trigger logic, unable to flexibly adapt to different trigger scenarios.

**This Innovation**:
- **Plugin Scan & Load**: Traverse plugin directory, verify plugin integrity, classify and store in corresponding plugin pools; supports hot-loading, adding/removing plugins without restart
- **Three-Mode Graded Scheduling**: Differentially invoke by trigger type (periodic/idle/idle_once)
- **Script Control & Result Parsing**: Subprocess isolated invocation, strictly parse trigger state (true=trigger, false=no trigger)
- **Priority Scheduling**: Mandatory plugins take priority over idle plugins to resolve concurrency conflicts
- **LLM Linkage**: After successful trigger, read ACTIVE.md and send to LLM, receive callback response and dispatch to execution layer

#### 5.3.3 Execution Layer (Interaction Implementation Layer)

**Note**: The execution layer reuses existing Agent framework tool execution capabilities (such as ReAct's Act part), this paper does not innovate.

**This Focus**:
- **Plugin Layer**: Trigger logic decoupled from kernel, supports plugin-based extension
- **Scheduling Layer**: Three-mode graded triggering, achieves proactive interaction
- **Ecosystem Support**: Plugin community loop, supports large-scale engineering implementation

---

## 6. Core Function Implementation

### 6.1 Proactive Interaction Implementation: Three-Mode Graded Triggering

**Proactive Interaction Core Philosophy**: Upgrade from "passive response" to "proactive prediction."

**Difference from "Continuous Sampling Input Proactive Interaction"**:

| Dimension | Continuous Sampling Input (e.g., File Monitor, Hazel) | This Architecture |
|-----------|------------------------------------------------------|-------------------|
| Positioning | **Perception Technical Means**: Polling/listening system state | **Engineering Framework**: Accommodates various perception methods |
| Trigger Logic | Fixed rules: if change → then action | Plugin-based: trigger returns true/false |
| Response Mode | Execute preset actions | Read ACTIVE.md → LLM generates response → Notify/Execute |
| Extension Mode | Requires modifying code/configuration rules | Add plugins, no kernel modification |
| Ecosystem Support | None, used in isolation | Plugin community loop |
| Iteration Capability | None, rules solidified | Plugins can be updated, feedback iteration |

> **Note**: Continuous sampling input (such as listening to files, mouse, system state) is a **perception technical means** and can be used as an implementation method for plugins in this paper. However, the core value of this architecture lies in providing an **engineering framework for proactive interaction**—the perception method is determined by the plugin, and after triggering, it links with the LLM to generate intelligent responses and achieves large-scale extension through the plugin ecosystem.

**This Focus**:
- **Plugin Framework**: Trigger logic decoupled from kernel, supports any perception method
- **LLM Linkage**: After triggering, read ACTIVE.md to generate scenario-based response
- **Ecosystem Loop**: Plugin community supports "development→review→distribution→feedback→iteration"

**Interaction Subjectivity Analysis**: User presets trigger rules, system proactively initiates interaction

| Dimension | Traditional Passive Interaction | This Architecture Interaction |
|-----------|--------------------------------|------------------------------|
| Trigger Subject | User command | Plugin script (extension of user will) |
| Interaction Initiator | User | System (Agent) |
| Example | User inputs `/help` → Agent responds | trigger returns true → Agent proactively notifies user |

**Interaction Flow**:
```
User Preset Phase:
  User downloads/writes plugin → Registers to active/ directory → Configures trigger conditions
  ↓
  Runtime Phase:
  Scheduler scans plugins → trigger script returns true → LLM generates response → Proactively notifies user
```

> **Note**: This is **user-preset system proactive interaction**, not LLM "autonomous consciousness" style proactive interaction. Plugin scripts are an extension of user will, and the LLM's "proactivity" is reflected in: when user-preset conditions are met, it generates responses **without requiring further user commands**. Analogy: Phone alarm clock—user sets the alarm, and the alarm proactively reminds the user when it rings.

### 6.1.1 Proactive Interaction Hierarchy Positioning

The "proactive interaction" of this architecture belongs to **engineering-feasible conditional proactivity**, distinct from sci-fi style "AI autonomous consciousness" proactive interaction.

**Proactive Interaction Hierarchy**:

```
┌─────────────────────────────────────────────────────────────┐
│  Proactivity Levels                                          │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ① Fully Passive ← Traditional Agents (ChatGPT):            │
│      Waiting for user commands                               │
│                                                              │
│  ② Conditional Proactive ← This Architecture:               │
│      User presets conditions, system proactively initiates   │
│                                                              │
│  ③ Autonomous Proactive ← Sci-Fi AI:                        │
│      Agent autonomously perceives, decides, and serves       │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

**Hierarchy Comparison**:

| Dimension | ① Fully Passive | ② Conditional Proactive (This Paper) | ③ Autonomous Proactive |
|-----------|----------------|--------------------------------------|-----------------------|
| Interaction Timing | User command | Condition met | Agent autonomous judgment |
| Trigger Decision Subject | User | User preset + System execution | Agent autonomous |
| Response Content Generation | Agent generated | Agent generated | Agent generated |
| Proactivity Level | Fully passive | Conditional proactive | Fully proactive |
| Engineering Feasibility | ✅ Mature | ✅ Feasible | ⚠️ Technology not mature |

**This Architecture Positioning**:
- ✅ **Interaction Timing Proactive**: No need for user to initiate command
- ✅ **Response Generation Proactive**: LLM generates scenario-based response
- ⚠️ **Trigger Condition Preset**: User defines concerns, not Agent autonomous judgment

> **Boundary Note**: This architecture is **engineering-feasible Agent proactive interaction**, achieving a balance between "user preset conditions" and "system proactive execution", avoiding over-interpretation as "AI autonomous consciousness".

**Three-Mode Graded Triggering Mechanism** (Comparison with Traditional Agents):

| Dimension | Traditional Agent | This Architecture |
|-----------|------------------|-------------------|
| Trigger Mode | Single: Only supports user command triggering | Three-mode: periodic/idle/idle_once |
| Event Grading | No grading, unified processing | Graded: Critical (mandatory) + normal (idle) |
| Do-Not-Disturb Support | Not supported | Supported: Interrupt normal triggering when user interacts |
| Single Trigger Support | No | Supported: idle_once (one-time guidance) |
| Timeout Protection | None | Supported: Script timeout 100ms to avoid blocking |

**Three Types of Trigger Modes**:

| Mode | Trigger Condition | Applicable Scenarios | Safeguard Mechanism |
|------|------------------|---------------------|---------------------|
| **Periodic Mandatory Trigger**<br>(periodic) | Trigger periodically according to configured interval | Critical events (hardware failures, etc.) | Script timeout 100ms to avoid blocking scheduling |
| **User Idle Trigger**<br>(idle) | Trigger when no interaction with LLM reaches threshold | Normal events (idle reminders, etc.) | Immediately interrupt when user resumes interaction |
| **Single Idle Trigger**<br>(idle_once) | Trigger once when no interaction with LLM reaches threshold | One-time suggestions (e.g., first-time user guidance) | Auto-disabled after one successful trigger |

**Configuration Example** (`trigger_mode.cfg`):

```ini
# ==========================================
# Periodic Mandatory Trigger (Critical Event)
# ==========================================
mode=periodic
interval=60        # Trigger once every 60 seconds
priority=10        # Priority: 0-100, smaller number = higher priority
timeout=100        # Script timeout 100ms

# ==========================================
# User Idle Trigger (Normal Event)
# ==========================================
mode=idle
threshold=300      # Trigger after 300 seconds of no interaction with LLM
interval=60        # Execute once every 60 seconds while idle
priority=50        # Medium priority
timeout=100

# ==========================================
# Single Idle Trigger (One-time Guidance)
# ==========================================
mode=idle_once
threshold=120      # Trigger after 120 seconds of no interaction with LLM
priority=40        # Medium-low priority
timeout=100
# Note: Plugin auto-disables after one successful trigger
# Manual reset required to re-enable
```

**Priority Explanation**:

| Priority | Range | Description | Example Scenarios |
|----------|-------|-------------|-------------------|
| Critical | 0-10 | System-level alarm, immediate execution, can interrupt user operations | Hardware failure, security threat |
| High | 11-30 | Important notifications, priority scheduling | Critical task reminders |
| Medium-High | 31-50 | Normal reminders, execute during user idle time | File organization, cache cleanup |
| Medium | 51-70 | Suggestion notifications, low disturbance | Usage tips recommendations |
| Low | 71-100 | Background tasks, lowest priority | Data synchronization, log archiving |

### 6.1.2 Multi-Period Independent Scheduler

The scheduling layer adopts a **multi-period independent scheduling strategy**, triggering each periodic plugin independently according to its configured `interval`, achieving precise scheduling and low CPU idle usage.

**Core Design**:

```
┌──────────────────────────────────────────────────────────────┐
│              Multi-Period Independent Scheduling Diagram      │
├──────────────────────────────────────────────────────────────┤
│                                                               │
│  Plugin Config           Scheduling Behavior                  │
│  ┌─────────────┐                                              │
│  │ Plugin A    │──► Trigger every 10 seconds                  │
│  │ interval=10 │                                              │
│  └─────────────┘                                              │
│                                                               │
│  ┌─────────────┐                                              │
│  │ Plugin B    │──► Trigger every 60 seconds                  │
│  │ interval=60 │                                              │
│  └─────────────┘                                              │
│                                                               │
│  ┌─────────────┐                                              │
│  │ Plugin C    │──► Trigger every 300 seconds                 │
│  │ interval=300│                                              │
│  └─────────────┘                                              │
│                                                               │
│  Scheduler: Calculate earliest due time → Sleep → Wake &     │
│             Execute → Loop                                    │
│                                                               │
└──────────────────────────────────────────────────────────────┘
```

**Scheduling Strategy Comparison**:

| Strategy | Implementation | Advantages | Disadvantages |
|----------|---------------|------------|---------------|
| Unified Scan Cycle | Fixed cycle (e.g., every 10 seconds) scans all plugins, checks if due | Simple implementation | CPU waste from idle spinning, low scheduling precision |
| **Independent Period Trigger** | Each plugin triggers independently according to its own `interval` | Precise scheduling, low CPU usage | Requires timer management |

**This architecture adopts independent period trigger strategy**, with advantages:

```
┌──────────────────────────────────────────────────────────────┐
│  Independent Period Scheduling Advantages                    │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  ✅ Precise Triggering: Each plugin executes independently   │
│     according to its own period, no interference             │
│                                                              │
│  ✅ Low CPU Usage: Timer sleep-wake mechanism, does not      │
│     occupy CPU                                               │
│                                                              │
│  ✅ High Extensibility: New plugins dynamically added to     │
│     scheduling queue, no restart required                    │
│                                                              │
│  ✅ Priority Assurance: Critical plugins can be scheduled    │
│     with higher priority                                     │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 6.1.3 Idle Trigger State Machine

Idle triggering adopts a **three-stage state machine** design, ensuring "no disturbance during user focus, proactive service during idle moments."

**Idle Definition**: "User idle" in this architecture refers to **no interaction with the LLM**, including:
- No conversation request (user has not sent new messages)
- No task execution (user has not triggered Agent tasks)
- No feedback operations (user has not rated/operated on LLM responses)

> **Note**: Using "interaction with LLM" rather than "system input events" as the basis for idle judgment avoids collecting privacy data such as mouse/keyboard input, and more accurately reflects the user's attention state toward the Agent.

**State Machine Description**:

| State | Description | Entry Condition | Exit Condition |
|-------|-------------|-----------------|----------------|
| ① Monitoring | Monitor user interaction events with LLM | Plugin start / User resumes interaction | No interaction with LLM ≥ threshold |
| ② Pending | Marked as triggerable, waiting for scheduling | No interaction with LLM ≥ threshold | Scheduling layer invokes |
| ③ Triggering | Execute trigger according to interval period | Scheduling layer invokes | User resumes interaction |

**Configuration Example** (`trigger_mode.cfg`):

```ini
# User Idle Trigger (Enhanced Configuration)
mode=idle
threshold=300      # No interaction with LLM for 300 seconds, enter idle trigger mode
interval=60        # After entering idle mode, execute trigger script every 60 seconds
priority=50        # Priority
timeout=100        # Script timeout 100ms
```

**Interaction Event Types**:

| Event Type | Description | Example |
|------------|-------------|---------|
| Conversation Request | User actively sends message | User inputs "/help" |
| Task Execution | User triggers Agent task | User clicks "Execute" button |
| Feedback Operation | User operates on response | Like/Dislike/Copy response |

**Advantages of Dual-Parameter Independent Control**:

| Parameter | Controls | Adjustment Effect |
|-----------|----------|-------------------|
| `threshold` | Threshold for entering idle mode | Larger = less sensitive, smaller = more sensitive |
| `interval` | Execution frequency in idle mode | Larger = less frequent, smaller = more frequent |

```
┌──────────────────────────────────────────────────────────────┐
│  Configuration Combination Examples                          │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Scenario 1: Light Disturbance (e.g., suggestion notifications)│
│  threshold=600, interval=300                                 │
│  → Trigger after 10 minutes of no interaction, detect every  │
│    5 minutes after triggering                                │
│                                                              │
│  Scenario 2: Medium Disturbance (e.g., file organization     │
│  reminder)                                                   │
│  threshold=300, interval=120                                 │
│  → Trigger after 5 minutes of no interaction, detect every   │
│    2 minutes after triggering                                │
│                                                              │
│  Scenario 3: Heavy Disturbance (e.g., sedentary reminder)    │
│  threshold=120, interval=60                                  │
│  → Trigger after 2 minutes of no interaction, detect every   │
│    minute after triggering                                   │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### 6.1.4 Single Idle Trigger Mode (idle_once)

Building on periodic and idle triggering, add a **single idle trigger** variant to meet "one-time guidance" scenario requirements.

**Core Philosophy**: Trigger once when no interaction with LLM reaches threshold, then auto-disable.

**Applicable Scenarios**:

| Scenario | Description | Example |
|----------|-------------|---------|
| New User Guidance | Provide guidance on first idle interaction | "First time using? Click to view feature introduction" |
| One-time Suggestion | Suggest once after detecting specific state | "Detected大量 unorganized files, organize them?" |
| Limited-time Reminder | No longer remind after user no response | "Idle for 2 hours, recommend rest" |

**Idle Definition**: Same as Section 6.1.3, refers to **no interaction with LLM** (no conversation request, no task execution, no feedback operation).

**State Machine Description**:

| State | Description | Entry Condition | Exit Condition |
|-------|-------------|-----------------|----------------|
| ① Monitoring | Monitor user interaction events with LLM | Plugin start | No interaction with LLM ≥ threshold |
| ② Pending | Marked as triggerable, waiting for scheduling | No interaction with LLM ≥ threshold | Scheduling layer invokes |
| ③ Triggering | Execute trigger, disable after returning true | Scheduling layer invokes | trigger returns true → Disabled |

**Difference between idle_once and idle**:

| Dimension | idle (Idle Periodic) | idle_once (Single Idle) |
|-----------|---------------------|-------------------------|
| Trigger Count | Multiple triggers during idle period | Trigger only once |
| Post-Trigger State | Continue monitoring, reset after user resumes interaction | Auto-disabled, requires manual reset |
| Typical Scenario | Continuous suggestions (e.g., cache cleanup) | One-time guidance (e.g., new user提示) |

**Manual Reset Methods**:

| Method | Description |
|--------|-------------|
| Delete plugin directory and re-download | Most thorough reset method |
| Modify mode field in trigger_mode.cfg | Quick reset |
| Reset via management command | e.g., `/active reset <plugin_name>` |
| Plugin implements self-reset logic | Reset after detecting specific file |

---

### 6.1.5 Three Trigger Modes Comparison

| Mode | Trigger Prerequisite | Trigger Count | Applicable Scenarios | Configuration Complexity |
|------|---------------------|---------------|---------------------|-------------------------|
| **periodic** | None, triggered by interval | Infinite | Continuous monitoring (CPU, memory) | Low |
| **idle** | No interaction with LLM ≥ threshold | Multiple during idle | Idle suggestions (cache cleanup) | Medium |
| **idle_once** | No interaction with LLM ≥ threshold | Only once | One-time guidance (new user提示) | Low |

**Selection Guidelines**:

**Quick Configuration**:

| Configuration Method | Description | Example |
|---------------------|-------------|---------|
| `threshold=0` | Skip idle detection, directly trigger periodically | `mode=idle, threshold=0` → Equivalent to periodic |
| `threshold>0` | Enable idle detection | `mode=idle, threshold=300` |
| `mode=idle_once` | Single idle trigger, auto-disable | `threshold=120` |

> **Note**: When `threshold=0` in idle mode, idle detection is automatically skipped, equivalent to periodic mode, no need to manually select trigger mode.

### 6.2 Plugin Implementation (Comparison with Traditional Solutions)

**Traditional Solutions Have No Proactive Plugin Design**: Traditional tools (Hazel, AutoHotkey, file monitors, etc.) use a **fixed rule engine** mode, rules are bound to programs, and proactive triggering capabilities cannot be extended through plugins.

| Dimension | Traditional Solutions (Hazel, AutoHotkey, etc.) | This Architecture |
|-----------|------------------------------------------------|-------------------|
| Architecture Mode | Fixed rule engine (if-then configuration) | Proactive plugin framework |
| Trigger | No standardized format, rules hard-coded in configuration | Standardized trigger script (returns true/false) |
| Extension Mode | Modify configuration or write new scripts | Add plugins, no kernel modification |
| Response Mode | Execute preset actions | LLM generates scenario-based response |
| Loading Mode | Takes effect after restart | Hot-loading (30-second automatic detection) |
| Ecosystem Support | None, used in isolation | Plugin community loop |

**Core Value of This Paper**: The first plugin-based framework for **proactive interaction scenarios**, upgrading scattered proactive trigger rules into a shareable and iterable engineering system.

### 6.3 LLM Linkage Implementation

**Core Flow After Trigger**:

```
trigger returns true → Read ACTIVE.md → Create temporary session → Agent Loop → Execute mode
```

**Three Response Modes**:

Types A/B/C are typical examples of proactive interaction modes; the architecture supports infinite extension through plugin design.

```
┌─────────────────────────────────────────────────────────────────┐
│  After trigger returns true: Execute one of three modes         │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  1. Create temporary session                                     │
│  2. Append system prompt from ACTIVE.md                          │
│  3. Append user message: "Trigger: <trigger_reason>"             │
│  4. Invoke Agent Loop                                            │
│  5. After LLM response, execute one of three modes:              │
│                                                                  │
│     ┌────────────────────┬────────────────────┬─────────────────┐
│     ▼                    ▼                    ▼                 │
│  Type A: Proactive   Type B: Execute     Type C: Silent         │
│  Interaction           Task + Notify       Execution            │
│  - LLM generates       - Agent executes    - Agent executes     │
│    reply content         tasks via           tasks silently     │
│  - Interact with         ToolRegistry      - No notification    │
│    user (dialogue +    - Notify user       - Session destroyed  │
│    emotional exchange)   upon completion   - Optional logging   │
│  - Session destroyed   - Session destroyed                      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

| Mode | Description | Example | Applicable Scenarios |
|------|-------------|---------|---------------------|
| **Type A: Proactive Interaction** | LLM generates reply content, interact with user (supports emotional exchange) | "CPU temperature too high, recommend checking cooling fan" / "Remember to take a break when working hard" | Warnings, suggestions, reminders, emotional support |
| **Type B: Execute Task + Notify** | Agent executes tasks via ToolRegistry based on ACTIVE.md configuration, notifies user upon completion | "Cleaned 5 background processes, CPU temperature dropped 3°C" | Tasks requiring user awareness (cleanup, maintenance) |
| **Type C: Silent Execution** | Agent executes tasks silently based on ACTIVE.md configuration, no notification | Background cleanup, scheduled sync | Background routine tasks (no user disturbance needed) |

### 6.3.1 Mode Selection Guidelines

| Decision Factor | Type A: Proactive Interaction | Type B: Execute Task + Notify | Type C: Silent Execution |
|----------------|------------------------------|-------------------------------|-------------------------|
| **ACTIVE.md contains tasks** | No | Yes | Yes |
| **Tool calling required** | No | Yes | Yes |
| **Notify user** | Yes, dialogue + emotional exchange | Yes, result notification | No |
| **Session lifecycle** | Create → Agent Loop → Destroy | Create → Agent Loop (with tools) → Notify → Destroy | Create → Agent Loop (with tools) → Destroy (silent) |
| **User expectation** | Receive information/suggestion | Action + result awareness | Complete without awareness |
| **Applicable scenarios** | Warnings, suggestions, reminders, emotional support | Tasks requiring user awareness (cleanup, maintenance) | Background routine tasks (no user disturbance needed) |

### 6.4 Plugin Ecosystem Loop Implementation

**Traditional Agent Problem**: No ecosystem support, plugins are developed in isolation, unable to form iterative optimization.

**This Core Support**:
- **Server-side**: Plugin upload, security review, classification management, version iteration, feedback collection
- **Client-side**: Plugin search, one-click download, automatic update, local management, feedback submission
- **Offline Adaptation**: Supports offline use of local plugins, automatically syncs data after network recovery

---

## 7. Proactive Interaction Value and Application Scenarios

### 7.1 Core Value: Three Major Breakthroughs from Traditional Agents to This Architecture

| Breakthrough | From... To... | Description |
|--------------|---------------|-------------|
| 1 | **Passive Response → Proactive Prediction** | No need for users to actively initiate commands, the system autonomously perceives scene changes and predicts needs |
| 2 | **Scattered Triggering → Loop Iteration** | Combines Agent Loop to record execution results and user feedback, completing iterative optimization |
| 3 | **Fixed Functionality → Plugin Extension** | Plugin framework + ecosystem loop, supports large-scale business scenario extension |

### 7.2 Typical Application Scenarios

| Scenario | Application Mode | Traditional Agent Limitations | This Architecture Advantages |
|----------|-----------------|------------------------------|------------------------------|
| **Industrial Control** | Periodically detect CPU temperature, memory usage, proactively trigger fault warnings | Requires user to actively query or configure complex rules | Plugin configuration, proactive warning |
| **Desktop** | Proactively remind file organization, cache cleanup during user idle time | Unable to judge user state, easily disturbing | Idle triggering, interrupt immediately when user resumes interaction |
| **IoT** | Automatically detect temperature, humidity, light, proactively link devices for control | Depends on fixed rules, unable to adapt to scenarios | LLM linkage, scenario-based response |
| **Intelligent Agent** | Supplement proactive triggering capability for Agent Loop | Agent Loop only supports passive calls | Proactive triggering + loop iteration |

### 7.3 Engineering Practice and Outlook

The architecture has been core implemented in the AiCode project. Future work will focus on standardization, scenario library construction, and active plugin ecosystem development.

---

## 8. Conclusion and Outlook

### 8.1 Innovation Summary

The core breakthrough of this architecture lies in **architectural-level innovation**, proposing systematic solutions for the core defects of traditional Agent design:

| Traditional Agent Defect | This Innovation Solution | Implementation Effect |
|-------------------------|-------------------------|----------------------|
| Trigger logic coupled with kernel | Plugin framework, trigger logic sinks to plugin layer | Plugin hot-swapping, zero kernel modification extension |
| Passive response architecture | **Proactive interaction framework** (three-mode graded triggering) | Proactive prediction, balances emergency response and do-not-disturb |
| No ecosystem loop support | Plugin community (development→review→distribution→feedback) | Sustainable iterative engineering system |
| Extension requires source code modification | Standardized plugin specification (3 core files) | Low-threshold development, large-scale extension |

**Core Innovations**:
1. **Proactive Interaction** (Core Value): The industry's first proactive interaction trigger framework—upgrading from "passive response" to "proactive prediction". Supports three typical interaction modes (extensible via plugins):
   - **Type A: Proactive Interaction**: LLM generates reply content to interact with user, supports emotional exchange (e.g., "CPU temperature too high, recommend checking cooling fan" / "Remember to take a break when working hard")
   - **Type B: Execute Task + Notify**: Agent executes tasks based on plugin configuration's ACTIVE.md via ToolRegistry, notifies user upon completion (e.g., "Cleaned 5 background processes, CPU temperature dropped 3°C")
   - **Type C: Silent Execution**: Agent executes tasks silently based on plugin configuration, no notification to user (e.g., background cleanup, scheduled sync)
2. **Plugin Framework** (Architecture Support): Trigger logic decoupled from kernel + plugin hot-swapping—solving the industry-wide pain point of "trigger logic coupled with kernel"
3. **Engineering Design** (Large-scale Guarantee): Plugin community loop + standardized plugin specification—supporting large-scale extension and sustainable iteration
4. **Three-Mode Triggering** (Technical Implementation): periodic/idle/idle_once three trigger modes, covering all scenarios including continuous monitoring, idle suggestions, and one-time guidance

### 8.2 Outlook

Future optimization will focus on architectural innovation, advancing in three directions:

1. **Standardization Promotion**: Promote proactive interaction plugin specifications to become industry standards
2. **Scenario Library Construction**: Accumulate proactive trigger scenario templates, lower plugin development threshold
3. **Active Plugin Ecosystem Development**: Build proactive interaction plugin community, supporting "development→review→distribution→feedback→iteration" loop

---

*Version: v1.0.0*
*Date: April 2026*
*Author: jiating.fang*
