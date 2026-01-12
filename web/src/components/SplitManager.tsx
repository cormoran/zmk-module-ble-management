/**
 * SplitManager Component
 *
 * Manages split keyboard connections (for central and peripheral halves).
 */

import { useContext, useState, useEffect, useCallback } from "react";
import {
  ZMKCustomSubsystem,
  ZMKAppContext,
} from "@cormoran/zmk-studio-react-hook";
import { SUBSYSTEM_IDENTIFIER } from "../App";
import {
  Request,
  Response,
  SplitInfo,
} from "../proto/zmk/ble_management/ble_management";
import "./SplitManager.css";

export function SplitManager() {
  const zmkApp = useContext(ZMKAppContext);
  const [splitInfo, setSplitInfo] = useState<SplitInfo | null>(null);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const subsystem = zmkApp?.findSubsystem(SUBSYSTEM_IDENTIFIER);

  // Load split info on mount
  const loadSplitInfo = useCallback(async () => {
    if (!zmkApp?.state.connection || !subsystem) return;

    setIsLoading(true);
    setError(null);

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      const request = Request.create({
        getSplitInfo: {},
      });

      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);
        if (resp.getSplitInfo?.info) {
          setSplitInfo(resp.getSplitInfo.info);
        } else if (resp.error) {
          setError(resp.error.message);
        }
      }
    } catch (err) {
      console.error("Failed to load split info:", err);
      setError(
        `Failed to load split info: ${err instanceof Error ? err.message : "Unknown error"}`
      );
    } finally {
      setIsLoading(false);
    }
  }, [zmkApp, subsystem]);

  useEffect(() => {
    if (subsystem && zmkApp?.state.connection) {
      loadSplitInfo();
    }
  }, [subsystem, zmkApp?.state.connection, loadSplitInfo]);

  const forgetSplitBond = async () => {
    if (!zmkApp?.state.connection || !subsystem) return;
    if (
      !confirm(
        "Are you sure you want to forget split keyboard bonds? This will clear all pairing information."
      )
    )
      return;

    setIsLoading(true);
    setError(null);

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      const request = Request.create({
        forgetSplitBond: {},
      });

      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);
        if (resp.forgetSplitBond?.success) {
          alert(
            "Split bonds cleared successfully. You may need to re-pair the keyboard halves."
          );
          await loadSplitInfo();
        } else if (resp.error) {
          setError(resp.error.message);
        } else {
          setError("Failed to forget split bonds");
        }
      }
    } catch (err) {
      console.error("Failed to forget split bonds:", err);
      setError(
        `Failed to forget split bonds: ${err instanceof Error ? err.message : "Unknown error"}`
      );
    } finally {
      setIsLoading(false);
    }
  };

  if (!subsystem) {
    return null;
  }

  // Don't show split manager if not a split keyboard
  if (!splitInfo?.isSplit) {
    return null;
  }

  return (
    <section className="card split-manager">
      <h2>⌨️ Split Keyboard</h2>

      {error && (
        <div className="error-message">
          <p>🚨 {error}</p>
        </div>
      )}

      {isLoading && !splitInfo && <p>⏳ Loading split information...</p>}

      {splitInfo && (
        <div className="split-info">
          <div className="info-grid">
            <div className="info-item">
              <strong>Role:</strong>{" "}
              {splitInfo.isCentral ? "Central" : "Peripheral"}
            </div>

            {splitInfo.isCentral && (
              <div className="info-item">
                <strong>Peripheral Status:</strong>{" "}
                {splitInfo.peripheralConnected ? (
                  <span className="status-connected">✓ Connected</span>
                ) : (
                  <span className="status-disconnected">✗ Disconnected</span>
                )}
              </div>
            )}

            {splitInfo.isPeripheral && (
              <div className="info-item">
                <strong>Central Bonded:</strong>{" "}
                {splitInfo.centralBonded ? (
                  <span className="status-bonded">✓ Bonded</span>
                ) : (
                  <span className="status-not-bonded">✗ Not Bonded</span>
                )}
              </div>
            )}
          </div>

          <div className="split-actions">
            <p className="warning-text">
              ⚠️ If you're experiencing connection issues between keyboard
              halves, you can reset the split connection below. This will clear
              all pairing information and you'll need to re-pair the halves.
            </p>
            <button
              className="btn btn-danger"
              onClick={forgetSplitBond}
              disabled={isLoading}
            >
              🔄 Reset Split Connection
            </button>
          </div>
        </div>
      )}

      <button
        className="btn btn-secondary"
        onClick={loadSplitInfo}
        disabled={isLoading}
      >
        🔄 Refresh
      </button>
    </section>
  );
}
