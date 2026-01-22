/**
 * Tests for ProfileManager component
 */

import { render, screen } from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { ProfileManager } from "../src/components/ProfileManager";
import { SUBSYSTEM_IDENTIFIER } from "../src/App";

// Mock console.error to avoid cluttering test output
const originalError = console.error;
beforeAll(() => {
  console.error = jest.fn();
});
afterAll(() => {
  console.error = originalError;
});

describe("ProfileManager Component", () => {
  describe("With Subsystem", () => {
    it("should render profile manager when subsystem is found", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <ProfileManager />
        </ZMKAppProvider>
      );

      expect(screen.getByText(/Bluetooth Profiles/i)).toBeInTheDocument();
      expect(screen.getByText(/Refresh/i)).toBeInTheDocument();
    });
  });

  describe("Without Subsystem", () => {
    it("should show warning when subsystem is not found", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <ProfileManager />
        </ZMKAppProvider>
      );

      expect(
        screen.getByText(/BLE Management subsystem not found/i)
      ).toBeInTheDocument();
    });
  });

  describe("Without ZMKAppContext", () => {
    it("should not render when ZMKAppContext is not provided", () => {
      render(<ProfileManager />);

      // Component should return a warning section when no context
      expect(
        screen.getByText(/BLE Management subsystem not found/i)
      ).toBeInTheDocument();
    });
  });
});
