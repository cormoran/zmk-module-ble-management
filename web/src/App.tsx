/**
 * BLE Management UI - Main Application
 *
 * This application provides a web interface for managing BLE connections on ZMK keyboards:
 * - View all paired devices
 * - Edit custom names for paired devices
 * - Switch between profiles
 * - Unpair devices
 * - Manage split keyboard connections
 */

import "./App.css";
import { connect as serial_connect } from "@zmkfirmware/zmk-studio-ts-client/transport/serial";
import { ZMKConnection } from "@cormoran/zmk-studio-react-hook";
import { ProfileManager } from "./components/ProfileManager";
import { SplitManager } from "./components/SplitManager";
import { OutputPriorityManager } from "./components/OutputPriorityManager";

export const SUBSYSTEM_IDENTIFIER = "zmk__ble_management";

function App() {
  return (
    <div className="app">
      <header className="app-header">
        <h1>📡 ZMK BLE Management</h1>
        <p>Manage Bluetooth connections for your ZMK keyboard</p>
      </header>

      <ZMKConnection
        renderDisconnected={({ connect, isLoading, error }) => (
          <section className="card">
            <h2>Device Connection</h2>
            <p>Connect your ZMK keyboard to manage Bluetooth profiles.</p>
            {isLoading && <p>⏳ Connecting...</p>}
            {error && (
              <div className="error-message">
                <p>🚨 {error}</p>
              </div>
            )}
            {!isLoading && (
              <button
                className="btn btn-primary"
                onClick={() => connect(serial_connect)}
              >
                🔌 Connect via Serial
              </button>
            )}
          </section>
        )}
        renderConnected={({ disconnect, deviceName }) => (
          <>
            <section className="card">
              <h2>Device Connection</h2>
              <div className="device-info">
                <h3>✅ Connected to: {deviceName}</h3>
              </div>
              <button className="btn btn-secondary" onClick={disconnect}>
                Disconnect
              </button>
            </section>

            <OutputPriorityManager />
            <ProfileManager />
            <SplitManager />
          </>
        )}
      />

      <footer className="app-footer">
        <p>
          <strong>ZMK BLE Management</strong> - Easily manage your keyboard's
          Bluetooth connections
        </p>
      </footer>
    </div>
  );
}

export default App;
