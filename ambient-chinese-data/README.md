# Ambient Chinese remote control plane

This directory hosts the data-only update channel for the Ambient Chinese v105+ Chrome extension.

`manifest.json` is the stable pointer. It references immutable/versioned JSON assets under `vNNN/` and includes SHA-256 hashes for every asset. The extension verifies the complete release before activation, retains the previous good release for rollback, and falls back to its bundled curriculum if remote data is unavailable.

Remote assets can change course patches, contextual grammar patches, learner-level presets, stage metadata, learning thresholds, replacement policy, selected feature flags, and AI tutor prompt templates. They are data only; executable JavaScript remains bundled with the extension to stay within Manifest V3 remote-code restrictions.

When publishing a new release, upload all versioned assets first and update the root `manifest.json` last.