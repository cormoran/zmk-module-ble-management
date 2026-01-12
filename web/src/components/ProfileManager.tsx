/**
 * ProfileManager Component
 *
 * Displays and manages BLE profiles for the connected keyboard.
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
  ProfileInfo,
} from "../proto/zmk/ble_management/ble_management";
import "./ProfileManager.css";

export function ProfileManager() {
  const zmkApp = useContext(ZMKAppContext);
  const [profiles, setProfiles] = useState<ProfileInfo[]>([]);
  const [maxProfiles, setMaxProfiles] = useState<number>(0);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [editingIndex, setEditingIndex] = useState<number | null>(null);
  const [editName, setEditName] = useState("");

  const subsystem = zmkApp?.findSubsystem(SUBSYSTEM_IDENTIFIER);

  // Load profiles on mount and when subsystem changes
  const loadProfiles = useCallback(
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
          getProfiles: {},
        });

        const payload = Request.encode(request).finish();
        const responsePayload = await service.callRPC(payload);

        if (responsePayload) {
          const resp = Response.decode(responsePayload);
          if (resp.getProfiles) {
            setProfiles(resp.getProfiles.profiles);
            setMaxProfiles(resp.getProfiles.maxProfiles);
          } else if (resp.error) {
            setError(resp.error.message);
          }
        }
      } catch (err) {
        console.error("Failed to load profiles:", err);
        setError(
          `Failed to load profiles: ${err instanceof Error ? err.message : "Unknown error"}`
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
      loadProfiles();
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [subsystem?.index, zmkApp?.state.connection, loadProfiles]);

  const switchProfile = async (index: number) => {
    if (!zmkApp?.state.connection || !subsystem) return;

    setIsLoading(true);
    setError(null);

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      const request = Request.create({
        switchProfile: { index },
      });

      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);
        if (resp.switchProfile?.success) {
          // Reload profiles to update active status
          await loadProfiles();
        } else if (resp.error) {
          setError(resp.error.message);
        } else {
          setError("Failed to switch profile");
        }
      }
    } catch (err) {
      console.error("Failed to switch profile:", err);
      setError(
        `Failed to switch profile: ${err instanceof Error ? err.message : "Unknown error"}`
      );
    } finally {
      setIsLoading(false);
    }
  };

  const unpairProfile = async (index: number) => {
    if (!zmkApp?.state.connection || !subsystem) return;
    if (!confirm("Are you sure you want to unpair this device?")) return;

    setIsLoading(true);
    setError(null);

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      const request = Request.create({
        unpairProfile: { index },
      });

      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);
        if (resp.unpairProfile?.success) {
          await loadProfiles();
        } else if (resp.error) {
          setError(resp.error.message);
        } else {
          setError("Failed to unpair profile");
        }
      }
    } catch (err) {
      console.error("Failed to unpair profile:", err);
      setError(
        `Failed to unpair profile: ${err instanceof Error ? err.message : "Unknown error"}`
      );
    } finally {
      setIsLoading(false);
    }
  };

  const saveProfileName = async (index: number, name: string) => {
    if (!zmkApp?.state.connection || !subsystem) return;

    setIsLoading(true);
    setError(null);

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      const request = Request.create({
        setProfileName: { index, name },
      });

      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);
        if (resp.setProfileName?.success) {
          setEditingIndex(null);
          setEditName("");
          await loadProfiles();
        } else if (resp.error) {
          setError(resp.error.message);
        } else {
          setError("Failed to save profile name");
        }
      }
    } catch (err) {
      console.error("Failed to save profile name:", err);
      setError(
        `Failed to save profile name: ${err instanceof Error ? err.message : "Unknown error"}`
      );
    } finally {
      setIsLoading(false);
    }
  };

  const startEditing = (index: number, currentName: string) => {
    setEditingIndex(index);
    setEditName(currentName);
  };

  const cancelEditing = () => {
    setEditingIndex(null);
    setEditName("");
  };

  if (!subsystem) {
    return (
      <section className="card">
        <div className="warning-message">
          <p>
            ⚠️ BLE Management subsystem not found. Make sure your firmware
            includes the BLE management module.
          </p>
        </div>
      </section>
    );
  }

  return (
    <section className="card profile-manager">
      <h2>📱 Bluetooth Profiles</h2>
      <p>
        Manage up to {maxProfiles} paired devices. Click on a profile to switch
        connections.
      </p>

      {error && (
        <div className="error-message">
          <p>🚨 {error}</p>
        </div>
      )}

      {isLoading && profiles.length === 0 && <p>⏳ Loading profiles...</p>}

      <div className="profiles-list">
        {profiles.map((profile) => (
          <div
            key={profile.index}
            className={`profile-item ${profile.isActive ? "active" : ""} ${
              profile.isConnected ? "connected" : ""
            } ${profile.isOpen ? "empty" : ""}`}
          >
            <div className="profile-header">
              <h3>
                Profile {profile.index + 1}
                {profile.isActive && " ⭐"}
                {profile.isConnected && " 🔗"}
              </h3>
              {profile.isActive && <span className="badge active">Active</span>}
              {profile.isConnected && !profile.isActive && (
                <span className="badge connected">Connected</span>
              )}
              {profile.isOpen && <span className="badge empty">Empty</span>}
            </div>

            {!profile.isOpen ? (
              <>
                <div className="profile-info">
                  {editingIndex === profile.index ? (
                    <div className="edit-name">
                      <input
                        type="text"
                        value={editName}
                        onChange={(e) => setEditName(e.target.value)}
                        placeholder="Device name"
                        maxLength={31}
                      />
                      <button
                        className="btn btn-sm btn-primary"
                        onClick={() => saveProfileName(profile.index, editName)}
                        disabled={isLoading}
                      >
                        ✓ Save
                      </button>
                      <button
                        className="btn btn-sm btn-secondary"
                        onClick={cancelEditing}
                        disabled={isLoading}
                      >
                        ✗ Cancel
                      </button>
                    </div>
                  ) : (
                    <>
                      <p className="profile-name">
                        <strong>Name:</strong>{" "}
                        {profile.name || (
                          <em className="text-muted">Not named</em>
                        )}
                        <button
                          className="btn btn-link"
                          onClick={() =>
                            startEditing(profile.index, profile.name)
                          }
                          disabled={isLoading}
                          title="Edit name"
                        >
                          ✏️
                        </button>
                      </p>
                      <p className="profile-address">
                        <strong>Address:</strong>{" "}
                        <code>{profile.address || "N/A"}</code>
                      </p>
                    </>
                  )}
                </div>

                <div className="profile-actions">
                  {!profile.isActive && (
                    <button
                      className="btn btn-primary"
                      onClick={() => switchProfile(profile.index)}
                      disabled={isLoading}
                    >
                      Switch to this profile
                    </button>
                  )}
                  <button
                    className="btn btn-danger"
                    onClick={() => unpairProfile(profile.index)}
                    disabled={isLoading}
                  >
                    🗑️ Unpair
                  </button>
                </div>
              </>
            ) : (
              <>
                <p className="text-muted">No device paired in this slot.</p>
                <div className="profile-actions">
                  {!profile.isActive && (
                    <button
                      className="btn btn-primary"
                      onClick={() => switchProfile(profile.index)}
                      disabled={isLoading}
                    >
                      Switch to this profile
                    </button>
                  )}
                </div>
              </>
            )}
          </div>
        ))}
      </div>

      <button
        className="btn btn-secondary"
        onClick={loadProfiles}
        disabled={isLoading}
      >
        🔄 Refresh
      </button>
    </section>
  );
}
