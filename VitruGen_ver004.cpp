/* Copyright (c) 2026, ANAHEIM SYSTEMS DYNAMICS CORPORATION.
 *
 * All rights reserved.
 *
 * Anaheim Systems Dynamics Software Product Development Project
 *
 * ---------------------------------------------------------------------------
 * PRODUCT INFORMATION
 * ---------------------------------------------------------------------------
 *
 * Product Title : VitruGen SIMCAD
 * Engine        : Euclid Engine
 * Version       : 0.0.4
 * Build Codename: Workspace Domain Architecture
 *
 * VitruGen runs on Euclid Engine.
 *
 * ---------------------------------------------------------------------------
 * VERSION 0.0.4 DEVELOPMENT IDENTITY
 * ---------------------------------------------------------------------------
 *
 * Version 0.0.4 begins the architectural transition of VitruGen SIMCAD from
 * a single 3D-grid application containing several modes into a domain-based
 * engineering workstation capable of hosting independent authoring,
 * simulation, visualization, and future runtime-preview workspaces.
 *
 * The primary objective of this release is architectural restructuring.
 *
 * Existing functionality from version 0.0.3 must remain operational while
 * being migrated into the new workspace-domain hierarchy.
 *
 * The principal workspace domains are:
 *
 *     GRID_2D
 *         Planar data, graphing, texture, image, and projection workspaces.
 *
 *     GRID_3D
 *         Structural authoring, surface generation, mesh CAD, and linked
 *         particle construction workspaces.
 *
 *     SIMCAD_4D
 *         Time-evolving simulations, cross-dimensional processing, physics,
 *         runtime validation, and future interactive sandbox workspaces.
 *
 * ---------------------------------------------------------------------------
 * CENTRAL ARCHITECTURAL PRINCIPLE
 * ---------------------------------------------------------------------------
 *
 * VitruGen uses a hierarchical, Tesseract-inspired workspace architecture.
 *
 *     Layer 0
 *         Global application shell and program context.
 *
 *     Layer 1
 *         Workspace-domain selection.
 *
 *     Layer 2
 *         Workspace or operational-mode selection and configuration.
 *
 *     Layer 3
 *         Active workspace execution.
 *
 *     Sub-Layers
 *         Internal workflow stages belonging to an active workspace.
 *
 *     Nodes
 *         Nested state-machine stages belonging to a complex operation.
 *
 * Complex VitruGen workflows should preferably follow the four-phase cycle:
 *
 *     CONTEXT -> SELECTION -> TRANSFORMATION -> RESOLUTION
 *
 * Completion returns the user to a stable context or preview state.
 *
 * ---------------------------------------------------------------------------
 * VITRUGEN SIMCAD VERSION 0.0.4 WORKSPACE HIERARCHY
 * ---------------------------------------------------------------------------
 *
 * LAYER 0 — IDLE / GLOBAL SHELL
 *
 *     Program configuration
 *     Workspace-domain selection
 *     Global resource and service availability
 *     File/project management                     [FUTURE]
 *     Application preview / Tesseract identity
 *
 * ---------------------------------------------------------------------------
 *
 * LAYER 1 — GRID_2D
 *
 *     LAYER 2 — GRAPH_2D
 *
 *         LAYER 3 — Active 2D graphing workspace
 *
 *         Initial version 0.0.4 scope:
 *
 *             Basic 2D coordinate grid
 *             Built-in mathematical functions
 *             Scale and domain controls
 *             Basic graph visualization
 *
 *     LAYER 2 — TEXTURE_MAP_2D                    [RESERVED]
 *
 *         Texture and material-map authoring.
 *
 *     LAYER 2 — SPRITE_PROJECTION_2D              [FUTURE]
 *
 *         Sprite animation and state projection onto 3D particle anchors.
 *
 * ---------------------------------------------------------------------------
 *
 * LAYER 1 — GRID_3D
 *
 *     LAYER 2 — GRAPH_3D
 *
 *         LAYER 3 — Active surface-authoring workspace
 *
 *             Mathematical surface generation
 *             Heightfield generation
 *             Terrain and map asset creation
 *             Future SANDBOX_SIM terrain export
 *
 *     LAYER 2 — SINGLE_PARTICLE_CAD
 *
 *         LAYER 3 — Active mesh-CAD workspace
 *
 *             SUB-LAYER 0 — Reference / runtime preview
 *
 *             SUB-LAYER 1 — Particle and workplane selection
 *
 *             SUB-LAYER 2 — Volume assembly
 *
 *                 Primitive editing
 *                 Fuse and Cut operations
 *                 Injection voxels
 *                 Assembly-node workflow
 *
 *                 NODE 0 — Preview
 *                 NODE 1 — Edit Object
 *                 NODE 2 — Offset / Position Object
 *                 NODE 3 — Apply / Commit to Base
 *
 *             SUB-LAYER 3 — Marching Cubes and OBJ export
 *
 *             Workflow completion returns to SUB-LAYER 0.
 *
 *     LAYER 2 — LINK_PARTICLES
 *
 *         LAYER 3 — Active linked-particle authoring workspace
 *
 *             Multiple particle anchors
 *             Parent and child relationships
 *             Joint and node transforms
 *             Modular object construction
 *             Molecular structures
 *             Mechanical assemblies
 *             Mechs and machines
 *             Animation and state-machine authoring       [FUTURE]
 *
 *         Initial version 0.0.4 status:
 *
 *             Architectural workspace reservation or placeholder.
 *
 * ---------------------------------------------------------------------------
 *
 * LAYER 1 — SIMCAD_4D
 *
 *     SIMCAD_4D contains simulation workspaces whose state evolves through
 *     time or whose data crosses between dimensional representations.
 *
 *     LAYER 2 — PARTICLE_SIMULATION
 *
 *         LAYER 3 — Original CUDA particle simulation
 *
 *         Version 0.0.4 objective:
 *
 *             Migrate the existing particle simulation into SIMCAD_4D.
 *             Preserve its baseline behavior during architectural migration.
 *
 *     LAYER 2 — NBODY_SIM                         [FUTURE]
 *
 *     LAYER 2 — FLUID_SIM                         [FUTURE]
 *
 *     LAYER 2 — CUDA_CAD                          [FUTURE]
 *
 *     LAYER 2 — SANDBOX_SIM                       [FUTURE]
 *
 *         Future integration and validation environment for assets authored
 *         in GRAPH_3D, SINGLE_PARTICLE_CAD, and LINK_PARTICLES.
 *
 *     LAYER 2 — ADDITIONAL TIME-EVOLVING SIMULATIONS
 *                                                  [FUTURE]
 *
 * ---------------------------------------------------------------------------
 * VERSION 0.0.4 CORE DEVELOPMENT COMMITMENTS
 * ---------------------------------------------------------------------------
 *
 * Version 0.0.4 should:
 *
 *     Introduce the GRID_2D, GRID_3D, and SIMCAD_4D domains.
 *
 *     Preserve the complete SINGLE_PARTICLE_CAD workflow.
 *
 *     Migrate the original particle simulation into SIMCAD_4D.
 *
 *     Provide at least one functional GRID_2D workspace.
 *
 *     Establish the architectural foundation for GRAPH_3D.
 *
 *     Reserve stable insertion points for LINK_PARTICLES and SANDBOX_SIM.
 *
 *     Expand TheTesseract into a general workspace host.
 *
 *     Generalize TheArbiter navigation and workspace selection.
 *
 *     Preserve EuclidEngine as the composition root and effect executor.
 *
 *     Preserve ViewPort as a projection of application and workspace state.
 *
 *     Preserve EuclidRenderer as the presentation and rendering subsystem.
 *
 *     Avoid unnecessary modification of stable CUDA simulation, volume,
 *     Marching Cubes, Fuse/Cut, and OBJ-export algorithms during migration.
 *
 * ---------------------------------------------------------------------------
 * VERSION 0.0.4 NON-GOALS
 * ---------------------------------------------------------------------------
 *
 * The following systems are not required for completion of version 0.0.4:
 *
 *     Full LINK_PARTICLES implementation
 *     Full SANDBOX_SIM implementation
 *     Standalone game-engine extraction
 *     Player and NPC systems
 *     Articulated robotics
 *     Terrain collision
 *     Texture-map editing
 *     Sprite projection
 *     N-body simulation
 *     Fluid simulation
 *     Licensing-safe algorithm rewrites
 *
 * These items remain planned future development campaigns.
 *
 * ---------------------------------------------------------------------------
 * DEVELOPMENT SAFETY RULE
 * ---------------------------------------------------------------------------
 *
 * Version 0.0.3 is the functional baseline.
 *
 * Architectural changes introduced in version 0.0.4 must be performed
 * incrementally, with compile-and-run regression checkpoints after each
 * meaningful migration step.
 *
 * Working subsystems should be migrated before they are redesigned.
 *
 * ---------------------------------------------------------------------------
 * THIRD-PARTY SOFTWARE NOTICE
 * ---------------------------------------------------------------------------
 *
 * Portions of the CUDA simulation, volume-processing, interoperability, and
 * Marching Cubes foundations may contain or derive from NVIDIA CUDA Samples
 * or other third-party software.
 *
 * Applicable copyright notices, license headers, attribution requirements,
 * and third-party documentation must be preserved where required.
 *
 * Anaheim Systems Dynamics retains ownership of its original VitruGen and
 * Euclid Engine architecture, interfaces, workflows, tools, modifications,
 * and independently authored source code, subject to applicable third-party
 * rights and licenses.
 */

#include "EuclidEngine.h" 

int main(int argc, char** argv) {
	EuclidEngine engine;
	if (!engine.init(argc, argv)) return 1;

	engine.run();

	return 0;
}