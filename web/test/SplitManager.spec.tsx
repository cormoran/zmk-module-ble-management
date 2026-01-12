/**
 * Tests for SplitManager component
 */

import { render } from "@testing-library/react";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { SplitManager } from "../src/components/SplitManager";
import { SUBSYSTEM_IDENTIFIER } from "../src/App";

// Mock console.error to avoid cluttering test output
const originalError = console.error;
beforeAll(() => {
  console.error = jest.fn();
});
afterAll(() => {
  console.error = originalError;
});

describe("SplitManager Component", () => {
  describe("With Subsystem", () => {
    it("should not render without split info (initially)", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });

      const { container } = render(
        <ZMKAppProvider value={mockZMKApp}>
          <SplitManager />
        </ZMKAppProvider>
      );

      // Component should not render initially until it determines if it's a split keyboard
      // Since we can't mock the RPC response easily, it will stay hidden
      expect(container.firstChild).toBeNull();
    });
  });

  describe("Without Subsystem", () => {
    it("should not render when subsystem is not found", () => {
      const mockZMKApp = createConnectedMockZMKApp({
        deviceName: "Test Device",
        subsystems: [],
      });

      const { container } = render(
        <ZMKAppProvider value={mockZMKApp}>
          <SplitManager />
        </ZMKAppProvider>
      );

      expect(container.firstChild).toBeNull();
    });
  });
});
