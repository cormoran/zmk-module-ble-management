/**
 * Tests for App component
 *
 * This test file demonstrates how to test the ZMK BLE Management application.
 */

import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { setupZMKMocks } from "@cormoran/zmk-studio-react-hook/testing";
import App from "../src/App";

// Mock the ZMK client
jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({
  create_rpc_connection: jest.fn(),
  call_rpc: jest.fn(),
}));

jest.mock("@zmkfirmware/zmk-studio-ts-client/transport/serial", () => ({
  connect: jest.fn(),
}));

describe("App Component", () => {
  describe("Basic Rendering", () => {
    it("should render the application header", () => {
      render(<App />);

      expect(
        screen.getByRole("heading", {
          name: /📡 ZMK BLE Management/i,
          level: 1,
        })
      ).toBeInTheDocument();
      expect(
        screen.getByText(/Manage Bluetooth connections/i)
      ).toBeInTheDocument();
    });

    it("should render connection button when disconnected", () => {
      render(<App />);

      expect(screen.getByText(/Connect via Serial/i)).toBeInTheDocument();
    });

    it("should render footer", () => {
      render(<App />);

      expect(
        screen.getByText(/Easily manage your keyboard's Bluetooth connections/i)
      ).toBeInTheDocument();
    });
  });

  describe("Connection Flow", () => {
    let mocks: ReturnType<typeof setupZMKMocks>;

    beforeEach(() => {
      mocks = setupZMKMocks();
    });

    it("should connect to device when connect button is clicked", async () => {
      // Set up successful connection mock
      mocks.mockSuccessfulConnection({
        deviceName: "Test Keyboard",
        subsystems: ["zmk__ble_management"],
      });

      // Mock the serial connect function
      const { connect: serial_connect } =
        await import("@zmkfirmware/zmk-studio-ts-client/transport/serial");
      (serial_connect as jest.Mock).mockResolvedValue(mocks.mockTransport);

      render(<App />);

      expect(screen.getByText(/Connect via Serial/i)).toBeInTheDocument();

      const user = userEvent.setup();
      const connectButton = screen.getByText(/Connect via Serial/i);
      await user.click(connectButton);

      await waitFor(() => {
        expect(
          screen.getByText(/Connected to: Test Keyboard/i)
        ).toBeInTheDocument();
      });

      expect(screen.getByText(/Disconnect/i)).toBeInTheDocument();
      expect(screen.getByText(/Bluetooth Profiles/i)).toBeInTheDocument();
    });
  });
});
