/**
 * OutputPriorityManager Component
 *
 * Manages output priority (transport) selection between USB and BLE.
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
  OutputPriority,
} from "../proto/zmk/ble_management/ble_management";
import "./OutputPriorityManager.css";

export function OutputPriorityManager() {
  const zmkApp = useContext(ZMKAppContext);
  const [currentPriority, setCurrentPriority] = useState<OutputPriority | null>(
    null
  );
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const subsystem = zmkApp?.findSubsystem(SUBSYSTEM_IDENTIFIER);

  // Load current output priority on mount
  const loadOutputPriority = useCallback(
    async () => {
      if (!zmkApp?.state.connection || !subsystem) return;

      setIsLoading(true);
      setError(null);

      try {
        const service = new ZMKCustomSubsystem(
          zmkApp.state.connection,
          subsystem.index
        );

        const request = Request.create({
          getOutputPriority: {},
        });

        const payload = Request.encode(request).finish();
        const responsePayload = await service.callRPC(payload);

        if (responsePayload) {
          const resp = Response.decode(responsePayload);
          if (resp.getOutputPriority) {
            setCurrentPriority(resp.getOutputPriority.priority);
          } else if (resp.error) {
            setError(resp.error.message);
          }
        }
      } catch (err) {
        console.error("Failed to load output priority:", err);
        setError(
          `Failed to load output priority: ${err instanceof Error ? err.message : "Unknown error"}`
        );
      } finally {
        setIsLoading(false);
      }
    },
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [zmkApp?.state.connection, subsystem?.index]
  );

  useEffect(() => {
    if (subsystem && zmkApp?.state.connection) {
      loadOutputPriority();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [subsystem?.index, zmkApp?.state.connection, loadOutputPriority]);

  const setOutputPriority = async (priority: OutputPriority) => {
    if (!zmkApp?.state.connection || !subsystem) return;

    setIsLoading(true);
    setError(null);

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      const request = Request.create({
        setOutputPriority: { priority },
      });

      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);
        if (resp.setOutputPriority?.success) {
          await loadOutputPriority();
        } else if (resp.error) {
          setError(resp.error.message);
        } else {
          setError("Failed to set output priority");
        }
      }
    } catch (err) {
      console.error("Failed to set output priority:", err);
      setError(
        `Failed to set output priority: ${err instanceof Error ? err.message : "Unknown error"}`
      );
    } finally {
      setIsLoading(false);
    }
  };

  if (!subsystem) {
    return null;
  }

  const priorityName =
    currentPriority === OutputPriority.OUTPUT_PRIORITY_USB ? "USB" : "BLE";

  return (
    <section className="card output-priority-manager">
      <h2>🔌 Output Priority</h2>
      <p>
        Choose the preferred output transport for your keyboard. When both USB
        and BLE are available, the selected transport will be used.
      </p>

      {error && (
        <div className="error-message">
          <p>🚨 {error}</p>
        </div>
      )}

      {isLoading && currentPriority === null && (
        <p>⏳ Loading output priority...</p>
      )}

      {currentPriority !== null && (
        <div className="priority-selection">
          <div className="current-priority">
            <strong>Current Priority:</strong>{" "}
            <span className={`priority-badge ${priorityName.toLowerCase()}`}>
              {priorityName}
            </span>
          </div>

          <div className="priority-options">
            <button
              className={`btn priority-btn ${
                currentPriority === OutputPriority.OUTPUT_PRIORITY_USB
                  ? "active"
                  : ""
              }`}
              onClick={() =>
                setOutputPriority(OutputPriority.OUTPUT_PRIORITY_USB)
              }
              disabled={
                isLoading ||
                currentPriority === OutputPriority.OUTPUT_PRIORITY_USB
              }
            >
              <span className="icon">🔌</span>
              <span className="label">USB</span>
              {currentPriority === OutputPriority.OUTPUT_PRIORITY_USB && (
                <span className="checkmark">✓</span>
              )}
            </button>

            <button
              className={`btn priority-btn ${
                currentPriority === OutputPriority.OUTPUT_PRIORITY_BLE
                  ? "active"
                  : ""
              }`}
              onClick={() =>
                setOutputPriority(OutputPriority.OUTPUT_PRIORITY_BLE)
              }
              disabled={
                isLoading ||
                currentPriority === OutputPriority.OUTPUT_PRIORITY_BLE
              }
            >
              <span className="icon">📡</span>
              <span className="label">Bluetooth (BLE)</span>
              {currentPriority === OutputPriority.OUTPUT_PRIORITY_BLE && (
                <span className="checkmark">✓</span>
              )}
            </button>
          </div>
        </div>
      )}

      <button
        className="btn btn-secondary"
        onClick={loadOutputPriority}
        disabled={isLoading}
      >
        🔄 Refresh
      </button>
    </section>
  );
}
