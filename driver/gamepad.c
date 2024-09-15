// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 Severin von Wnuck-Lipinski <severinvonw@outlook.de>
 */

#include <linux/module.h>
#include <linux/uuid.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#include "common.h"
#include "../auth/auth.h"

#define GIP_GP_NAME "Microsoft Xbox Controller"

#define GIP_GP_RUMBLE_DELAY msecs_to_jiffies(10)
#define GIP_GP_RUMBLE_MAX 100

/* button offset from end of packet */
#define GIP_GP_BTN_SHARE_OFFSET 18

static const guid_t gip_gamepad_guid_share =
  GUID_INIT(0xecddd2fe, 0xd387, 0x4294,
      0xbd, 0x96, 0x1a, 0x71, 0x2e, 0x3d, 0xc7, 0x7d);

static const guid_t gip_gamepad_guid_dli =
  GUID_INIT(0x87f2e56b, 0xc3bb, 0x49b1,
      0x82, 0x65, 0xff, 0xff, 0xf3, 0x77, 0x99, 0xee);

enum gip_probe_states {
  GIP_STATE_START     = 0x00,
  GIP_STATE_PAIR      = 0x01,
  GIP_STATE_POWER     = 0x10,
  GIP_STATE_BATTERY   = 0x20,
  GIP_STATE_LED       = 0x30,
  GIP_STATE_HANDSHAKE = 0x40,
  GIP_STATE_INPUT     = 0x50,
  GIP_STATE_READY     = 0x60,
  GIP_STATE_FINISH    = 0x61,
};

enum gip_gamepad_button {
  GIP_GP_BTN_MENU = BIT(2),
  GIP_GP_BTN_VIEW = BIT(3),
  GIP_GP_BTN_A = BIT(4),
  GIP_GP_BTN_B = BIT(5),
  GIP_GP_BTN_X = BIT(6),
  GIP_GP_BTN_Y = BIT(7),
  GIP_GP_BTN_DPAD_U = BIT(8),
  GIP_GP_BTN_DPAD_D = BIT(9),
  GIP_GP_BTN_DPAD_L = BIT(10),
  GIP_GP_BTN_DPAD_R = BIT(11),
  GIP_GP_BTN_BUMPER_L = BIT(12),
  GIP_GP_BTN_BUMPER_R = BIT(13),
  GIP_GP_BTN_STICK_L = BIT(14),
  GIP_GP_BTN_STICK_R = BIT(15),
};

enum gip_gamepad_motor {
  GIP_GP_MOTOR_R = BIT(0),
  GIP_GP_MOTOR_L = BIT(1),
  GIP_GP_MOTOR_RT = BIT(2),
  GIP_GP_MOTOR_LT = BIT(3),
};

enum gip_init_state {
  GIP_GP_WAIT_STATUS       = 0x00,
  GIP_GP_AUTHENTICATING    = 0x10,
  GIP_GP_AUTHENTICATED     = 0x20,
  GIP_GP_READY             = 0xFF,
};

struct gip_gamepad_pkt_input {
  __le16 buttons;
  __le16 trigger_left;
  __le16 trigger_right;
  __le16 stick_left_x;
  __le16 stick_left_y;
  __le16 stick_right_x;
  __le16 stick_right_y;
} __packed;

struct gip_gamepad_pkt_dli {
  u32 counter_us1;
  u32 counter_us2;
} __packed;

struct gip_gamepad_pkt_rumble {
  u8 unknown;
  u8 motors;
  u8 left_trigger;
  u8 right_trigger;
  u8 left;
  u8 right;
  u8 duration;
  u8 delay;
  u8 repeat;
} __packed;

struct gip_gamepad {
  struct gip_client *client;
  struct gip_battery battery;
  struct gip_auth auth;
  struct gip_led led;
  struct gip_input input;

<<<<<<< HEAD
	bool supports_share;
	bool supports_dli;
  
  u8 current_state;

	struct gip_gamepad_rumble {
		/* serializes access to rumble packet */
		spinlock_t lock;
		unsigned long last;
		struct timer_list timer;
		struct gip_gamepad_pkt_rumble pkt;
	} rumble;

  struct delayed_work dwork;
	struct workqueue_struct *event_wq;
=======
  u8   state;

  bool supports_share;
  bool supports_dli;

  struct gip_gamepad_rumble {
    /* serializes access to rumble packet */
    spinlock_t lock;
    unsigned long last;
    struct timer_list timer;
    struct gip_gamepad_pkt_rumble pkt;
  } rumble;

  struct work_struct work;
>>>>>>> PR_rework2
};

static void gip_gamepad_send_rumble(struct timer_list *timer)
{
  struct gip_gamepad_rumble *rumble = from_timer(rumble, timer, timer);
  struct gip_gamepad *gamepad = container_of(rumble, typeof(*gamepad),
               rumble);
  unsigned long flags;

  spin_lock_irqsave(&rumble->lock, flags);

  gip_send_rumble(gamepad->client, &rumble->pkt, sizeof(rumble->pkt));
  rumble->last = jiffies;

  spin_unlock_irqrestore(&rumble->lock, flags);
}

static int gip_gamepad_queue_rumble(struct input_dev *dev, void *data,
            struct ff_effect *effect)
{
  struct gip_gamepad_rumble *rumble = input_get_drvdata(dev);
  u32 mag_left = effect->u.rumble.strong_magnitude;
  u32 mag_right = effect->u.rumble.weak_magnitude;
  unsigned long flags;

  if (effect->type != FF_RUMBLE)
    return 0;

  spin_lock_irqsave(&rumble->lock, flags);

  rumble->pkt.left = (mag_left * GIP_GP_RUMBLE_MAX + S16_MAX) / U16_MAX;
  rumble->pkt.right = (mag_right * GIP_GP_RUMBLE_MAX + S16_MAX) / U16_MAX;

  /* delay rumble to work around firmware bug */
  if (!timer_pending(&rumble->timer))
    mod_timer(&rumble->timer, rumble->last + GIP_GP_RUMBLE_DELAY);

  spin_unlock_irqrestore(&rumble->lock, flags);

  return 0;
}

static int gip_gamepad_init_rumble(struct gip_gamepad *gamepad)
{
  struct gip_gamepad_rumble *rumble = &gamepad->rumble;
  struct input_dev *dev = gamepad->input.dev;

  spin_lock_init(&rumble->lock);
  timer_setup(&rumble->timer, gip_gamepad_send_rumble, 0);

  /* stop rumble (required for some exotic gamepads to start input) */
  rumble->pkt.motors = GIP_GP_MOTOR_R | GIP_GP_MOTOR_L |
           GIP_GP_MOTOR_RT | GIP_GP_MOTOR_LT;
  rumble->pkt.duration = 0xff;
  rumble->pkt.repeat = 0xeb;
  gip_gamepad_send_rumble(&rumble->timer);

  input_set_capability(dev, EV_FF, FF_RUMBLE);
  input_set_drvdata(dev, rumble);

  return input_ff_create_memless(dev, NULL, gip_gamepad_queue_rumble);
}

static int gip_gamepad_init_input(struct gip_gamepad *gamepad)
{
  struct input_dev *dev = gamepad->input.dev;
  int err;

  gamepad->supports_share = gip_has_interface(gamepad->client,
                &gip_gamepad_guid_share);
  gamepad->supports_dli = gip_has_interface(gamepad->client,
              &gip_gamepad_guid_dli);

  if (gamepad->supports_share)
    input_set_capability(dev, EV_KEY, KEY_RECORD);

  input_set_capability(dev, EV_KEY, BTN_MODE);
  input_set_capability(dev, EV_KEY, BTN_START);
  input_set_capability(dev, EV_KEY, BTN_SELECT);
  input_set_capability(dev, EV_KEY, BTN_A);
  input_set_capability(dev, EV_KEY, BTN_B);
  input_set_capability(dev, EV_KEY, BTN_X);
  input_set_capability(dev, EV_KEY, BTN_Y);
  input_set_capability(dev, EV_KEY, BTN_TL);
  input_set_capability(dev, EV_KEY, BTN_TR);
  input_set_capability(dev, EV_KEY, BTN_THUMBL);
  input_set_capability(dev, EV_KEY, BTN_THUMBR);
  input_set_abs_params(dev, ABS_X, -32768, 32767, 16, 128);
  input_set_abs_params(dev, ABS_RX, -32768, 32767, 16, 128);
  input_set_abs_params(dev, ABS_Y, -32768, 32767, 16, 128);
  input_set_abs_params(dev, ABS_RY, -32768, 32767, 16, 128);
  input_set_abs_params(dev, ABS_Z, 0, 1023, 0, 0);
  input_set_abs_params(dev, ABS_RZ, 0, 1023, 0, 0);
  input_set_abs_params(dev, ABS_HAT0X, -1, 1, 0, 0);
  input_set_abs_params(dev, ABS_HAT0Y, -1, 1, 0, 0);

  err = gip_gamepad_init_rumble(gamepad);
  if (err) {
    dev_err(&gamepad->client->dev, "%s: init rumble failed: %d\n",
      __func__, err);
    goto err_delete_timer;
  }

  err = input_register_device(dev);
  if (err) {
    dev_err(&gamepad->client->dev, "%s: register failed: %d\n",
      __func__, err);
    goto err_delete_timer;
  }

  return 0;

err_delete_timer:
  del_timer_sync(&gamepad->rumble.timer);

  return err;
}

static int gip_gamepad_op_battery(struct gip_client *client,
          enum gip_battery_type type,
          enum gip_battery_level level)
{
  struct gip_gamepad *gamepad = dev_get_drvdata(&client->dev);

  gip_report_battery(&gamepad->battery, type, level);

  // if this is the 1st status message then start handshake
  if (gamepad->state == GIP_GP_WAIT_STATUS)
    schedule_work(&gamepad->work);

  return 0;
}

static int gip_gamepad_op_authenticate(struct gip_client *client,
               void *data, u32 len)
{
  struct gip_gamepad *gamepad = dev_get_drvdata(&client->dev);

  return gip_auth_process_pkt(&gamepad->auth, data, len);
}

static int gip_gamepad_op_authenticated(struct gip_client *client, bool success)
{
  struct gip_gamepad *gamepad = dev_get_drvdata(&client->dev);
  
  if (success)
  {
    gamepad->state = GIP_GP_AUTHENTICATED;
    int err = gip_init_input(&gamepad->input, gamepad->client, GIP_GP_NAME);
    if (err)
      return err;
    err = gip_gamepad_init_input(gamepad);
    if (err)
      return err;
    gamepad->state = GIP_GP_READY;
  }
  return 0;
}

static int gip_gamepad_op_guide_button(struct gip_client *client, bool down)
{
  struct gip_gamepad *gamepad = dev_get_drvdata(&client->dev);

  input_report_key(gamepad->input.dev, BTN_MODE, down);
  input_sync(gamepad->input.dev);

  return 0;
}

static int gip_gamepad_op_input(struct gip_client *client, void *data, u32 len)
{
  struct gip_gamepad *gamepad = dev_get_drvdata(&client->dev);
  struct gip_gamepad_pkt_input *pkt = data;
  struct input_dev *dev = gamepad->input.dev;
  u16 buttons;
  u8 share_offset = GIP_GP_BTN_SHARE_OFFSET;

  if (len < sizeof(*pkt))
    return -EINVAL;

  buttons = le16_to_cpu(pkt->buttons);

  /* share button byte is always at fixed offset from end of packet */
  if (gamepad->supports_share) {
    if (gamepad->supports_dli)
      share_offset += sizeof(struct gip_gamepad_pkt_dli);

    if (len < share_offset)
      return -EINVAL;

    input_report_key(dev, KEY_RECORD,
         ((u8 *)data)[len - share_offset]);
  }

  input_report_key(dev, BTN_START, buttons & GIP_GP_BTN_MENU);
  input_report_key(dev, BTN_SELECT, buttons & GIP_GP_BTN_VIEW);
  input_report_key(dev, BTN_A, buttons & GIP_GP_BTN_A);
  input_report_key(dev, BTN_B, buttons & GIP_GP_BTN_B);
  input_report_key(dev, BTN_X, buttons & GIP_GP_BTN_X);
  input_report_key(dev, BTN_Y, buttons & GIP_GP_BTN_Y);
  input_report_key(dev, BTN_TL, buttons & GIP_GP_BTN_BUMPER_L);
  input_report_key(dev, BTN_TR, buttons & GIP_GP_BTN_BUMPER_R);
  input_report_key(dev, BTN_THUMBL, buttons & GIP_GP_BTN_STICK_L);
  input_report_key(dev, BTN_THUMBR, buttons & GIP_GP_BTN_STICK_R);
  input_report_abs(dev, ABS_X, (s16)le16_to_cpu(pkt->stick_left_x));
  input_report_abs(dev, ABS_RX, (s16)le16_to_cpu(pkt->stick_right_x));
  input_report_abs(dev, ABS_Y, ~(s16)le16_to_cpu(pkt->stick_left_y));
  input_report_abs(dev, ABS_RY, ~(s16)le16_to_cpu(pkt->stick_right_y));
  input_report_abs(dev, ABS_Z, le16_to_cpu(pkt->trigger_left));
  input_report_abs(dev, ABS_RZ, le16_to_cpu(pkt->trigger_right));
  input_report_abs(dev, ABS_HAT0X, !!(buttons & GIP_GP_BTN_DPAD_R) -
           !!(buttons & GIP_GP_BTN_DPAD_L));
  input_report_abs(dev, ABS_HAT0Y, !!(buttons & GIP_GP_BTN_DPAD_D) -
           !!(buttons & GIP_GP_BTN_DPAD_U));
  input_sync(dev);

  return 0;
}

static void gip_gamepad_start_handshake(struct work_struct *work)
{
  struct gip_gamepad *gamepad = container_of(work, struct gip_gamepad, work);

  int err = gip_auth_start_handshake(&gamepad->auth, gamepad->client);
  if (err)
    return;
  gamepad->state = GIP_GP_AUTHENTICATING;
}

static void gip_gamepad_state_transition(struct work_struct *work)
{
	struct gip_gamepad *gamepad = container_of(to_delayed_work(work),
						  typeof(*gamepad),
						  dwork);

	int err;
  unsigned long delay_us = 500;

  switch (gamepad->current_state)
  {
    case GIP_STATE_START:
	    err = gip_set_led_mode(gamepad->client,GIP_LED_BLINK_NORMAL,20);
      //gip_init_led(&gamepad->led, gamepad->client);
      gamepad->current_state = GIP_STATE_PAIR;
      break;
    case GIP_STATE_PAIR:
      err = gip_set_power_mode_pairing(gamepad->client);
      gamepad->current_state = GIP_STATE_POWER;
      break;
    case GIP_STATE_POWER:
	    err = gip_set_power_mode(gamepad->client, GIP_PWR_ON);
      gamepad->current_state = GIP_STATE_BATTERY;
      break;
    case GIP_STATE_BATTERY:
	    err = gip_init_battery(&gamepad->battery, gamepad->client, GIP_GP_NAME);
      gamepad->current_state = GIP_STATE_LED;
      break;
    case GIP_STATE_LED:
	    err = gip_set_led_mode(gamepad->client,GIP_LED_BLINK_FAST,20);
	    //err = gip_init_led(&gamepad->led, gamepad->client);
      gamepad->current_state = GIP_STATE_HANDSHAKE;
      break;
    case GIP_STATE_HANDSHAKE:
	    err = gip_auth_start_handshake(&gamepad->auth, gamepad->client);
      gamepad->current_state = GIP_STATE_INPUT;
      delay_us = 4000; 
      break;
    case GIP_STATE_INPUT:
	    err = gip_init_input(&gamepad->input, gamepad->client, GIP_GP_NAME);
      if (err == 0)
      {
        err = gip_gamepad_init_input(gamepad);
      }
      gamepad->current_state = GIP_STATE_READY;
      break;
    case GIP_STATE_READY:
	    err = gip_init_led(&gamepad->led, gamepad->client);
      gamepad->current_state = GIP_STATE_FINISH;
      break;
    default:
      err = -EINVAL;
      break;
  }
  if (err)
  {
    dev_err(&gamepad->client->dev, "%s: failed at state %d: %d\n", __func__, gamepad->current_state, err);
  }
  else if(gamepad->current_state != GIP_STATE_FINISH) 
  {
    schedule_delayed_work(&gamepad->dwork, msecs_to_jiffies(delay_us));
  }
}

static int gip_gamepad_probe(struct gip_client *client)
{
  struct gip_gamepad *gamepad;
  int err;

  gamepad = devm_kzalloc(&client->dev, sizeof(*gamepad), GFP_KERNEL);
  if (!gamepad)
    return -ENOMEM;

<<<<<<< HEAD
	gamepad->client = client;
  gamepad->current_state = GIP_STATE_START;

	dev_set_drvdata(&client->dev, gamepad);

  /* start a delayed work for actual input initialization */
  INIT_DELAYED_WORK(&gamepad->dwork, gip_gamepad_state_transition);
  schedule_delayed_work(&gamepad->dwork, msecs_to_jiffies(1000));

	return 0;
=======
  INIT_WORK(&gamepad->work, gip_gamepad_start_handshake);

  gamepad->client = client;
  gamepad->state = GIP_GP_WAIT_STATUS;

  err = gip_set_power_mode(client, GIP_PWR_ON);
  if (err)
    return err;

  err = gip_init_battery(&gamepad->battery, client, GIP_GP_NAME);
  if (err)
    return err;

  err = gip_init_led(&gamepad->led, client);
  if (err)
    return err;

  dev_set_drvdata(&client->dev, gamepad);

  return 0;
>>>>>>> PR_rework2
}

static void gip_gamepad_remove(struct gip_client *client)
{
  struct gip_gamepad *gamepad = dev_get_drvdata(&client->dev);

<<<<<<< HEAD
	cancel_delayed_work_sync(&gamepad->dwork);

	del_timer_sync(&gamepad->rumble.timer);
=======
  cancel_work_sync(&gamepad->work);

  del_timer_sync(&gamepad->rumble.timer);
>>>>>>> PR_rework2
}

static struct gip_driver gip_gamepad_driver = {
  .name = "xone-gip-gamepad",
  .class = "Windows.Xbox.Input.Gamepad",
  .ops = {
    .battery = gip_gamepad_op_battery,
    .authenticate = gip_gamepad_op_authenticate,
    .authenticated = gip_gamepad_op_authenticated,
    .guide_button = gip_gamepad_op_guide_button,
    .input = gip_gamepad_op_input,
  },
  .probe = gip_gamepad_probe,
  .remove = gip_gamepad_remove,
};
module_gip_driver(gip_gamepad_driver);

MODULE_ALIAS("gip:Windows.Xbox.Input.Gamepad");
MODULE_AUTHOR("Severin von Wnuck-Lipinski <severinvonw@outlook.de>");
MODULE_DESCRIPTION("xone GIP gamepad driver");
MODULE_VERSION("#VERSION#");
MODULE_LICENSE("GPL");
