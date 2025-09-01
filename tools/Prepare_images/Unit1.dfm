object Form1: TForm1
  Left = 0
  Top = 0
  BorderIcons = [biSystemMenu, biMinimize]
  BorderStyle = bsSingle
  Caption = 'Clock image converter'
  ClientHeight = 318
  ClientWidth = 330
  Color = clBtnFace
  Font.Charset = DEFAULT_CHARSET
  Font.Color = clWindowText
  Font.Height = -11
  Font.Name = 'Tahoma'
  Font.Style = []
  OldCreateOrder = False
  PixelsPerInch = 96
  TextHeight = 13
  object lbl1: TLabel
    Left = 160
    Top = 32
    Width = 86
    Height = 13
    Caption = 'max 135 x 240 px'
  end
  object lblSize: TLabel
    Left = 24
    Top = 63
    Width = 29
    Height = 13
    Caption = 'lblSize'
  end
  object lbl2: TLabel
    Left = 24
    Top = 8
    Width = 297
    Height = 13
    Caption = 'Convert image into 16 BPP file for custom EleksTube clock'
  end
  object lbl3: TLabel
    Left = 24
    Top = 102
    Width = 31
    Height = 13
    Caption = 'Name:'
  end
  object lblSaved: TLabel
    Left = 24
    Top = 157
    Width = 4
    Height = 13
    Caption = '.'
  end
  object btnOpen: TButton
    Left = 24
    Top = 32
    Width = 75
    Height = 25
    Caption = 'Open'
    TabOrder = 0
    OnClick = btnOpenClick
  end
  object btnSave: TButton
    Left = 24
    Top = 126
    Width = 75
    Height = 25
    Caption = 'Save'
    Enabled = False
    TabOrder = 1
    OnClick = btnSaveClick
  end
  object pnl1: TPanel
    Left = 136
    Top = 48
    Width = 145
    Height = 250
    BevelOuter = bvLowered
    TabOrder = 2
    object img1: TImage
      Left = 5
      Top = 5
      Width = 135
      Height = 240
      Proportional = True
    end
  end
  object txt1: TStaticText
    Left = 8
    Top = 185
    Width = 106
    Height = 113
    AutoSize = False
    Caption = 
      'Output file format: Header 6 bytes ("CK", Width, Height), follow' +
      'ed by image data, 16 bits per pixel in R5, G6, B5 format. (0,0) ' +
      'is top left corner.'
    TabOrder = 3
  end
  object edName: TEdit
    Left = 61
    Top = 99
    Width = 38
    Height = 21
    TabOrder = 4
    Text = '10'
  end
  object dlgOpenPic1: TOpenPictureDialog
    Filter = 
      'All (*.gif;*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.ico;*.emf;*.w' +
      'mf)|*.gif;*.jpg;*.jpeg;*.png;*.bmp;*.tif;*.tiff;*.ico;*.emf;*.wm' +
      'f|GIF Image (*.gif)|*.gif|JPEG Image File (*.jpg)|*.jpg|JPEG Ima' +
      'ge File (*.jpeg)|*.jpeg|Portable Network Graphics (*.png)|*.png|' +
      'Bitmaps (*.bmp)|*.bmp|TIFF Images (*.tif)|*.tif|TIFF Images (*.t' +
      'iff)|*.tiff|Icons (*.ico)|*.ico|Enhanced Metafiles (*.emf)|*.emf' +
      '|Metafiles (*.wmf)|*.wmf'
    Options = [ofHideReadOnly, ofAllowMultiSelect, ofPathMustExist, ofFileMustExist, ofNoTestFileCreate, ofEnableSizing]
    Left = 224
    Top = 72
  end
end
